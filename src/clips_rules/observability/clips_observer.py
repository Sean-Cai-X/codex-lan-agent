#!/usr/bin/env python3
"""Offline CLIPS rule and MCP flow observer.

This script is intentionally outside the C++ dispatch layer. It parses CLIPS
rule files and flow JSON files, then emits graph/report artifacts for review.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable


KEYWORDS = {
    "assert",
    "bind",
    "declare",
    "if",
    "not",
    "or",
    "and",
    "test",
}


@dataclass
class TemplateInfo:
    name: str
    path: str
    slots: list[str] = field(default_factory=list)


@dataclass
class RuleInfo:
    name: str
    path: str
    salience: int = 0
    lhs_facts: list[str] = field(default_factory=list)
    rhs_asserts: list[str] = field(default_factory=list)
    decisions: list[str] = field(default_factory=list)
    route_targets: list[str] = field(default_factory=list)
    reason_codes: list[str] = field(default_factory=list)


@dataclass
class FlowInfo:
    flow_id: str
    path: str
    cpp_role: str = ""
    states: list[str] = field(default_factory=list)
    forbidden_transitions: list[dict] = field(default_factory=list)


def strip_comments(text: str) -> str:
    lines: list[str] = []
    for line in text.splitlines():
        in_string = False
        escaped = False
        kept: list[str] = []
        for char in line:
            if char == '"' and not escaped:
                in_string = not in_string
            if char == ";" and not in_string:
                break
            kept.append(char)
            escaped = char == "\\" and not escaped
        lines.append("".join(kept))
    return "\n".join(lines)


def top_level_forms(text: str) -> list[str]:
    forms: list[str] = []
    start = -1
    depth = 0
    in_string = False
    escaped = False
    for index, char in enumerate(text):
        if char == '"' and not escaped:
            in_string = not in_string
        if in_string:
            escaped = char == "\\" and not escaped
            continue
        escaped = False
        if char == "(":
            if depth == 0:
                start = index
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0 and start >= 0:
                forms.append(text[start : index + 1])
                start = -1
    return forms


def head_symbol(form: str) -> str:
    match = re.match(r"\(\s*([A-Za-z_][\w-]*)", form)
    return match.group(1) if match else ""


def form_tail_after_name(form: str, construct: str, name: str) -> str:
    prefix = re.match(
        r"\(\s*" + re.escape(construct) + r"\s+" + re.escape(name) + r"\s*",
        form,
        flags=re.S,
    )
    if not prefix:
        return ""
    return form[prefix.end() : -1]


def extract_first_level_facts(lhs: str) -> list[str]:
    facts: list[str] = []
    for form in top_level_forms(lhs):
        symbol = head_symbol(form)
        if symbol and symbol not in KEYWORDS:
            facts.append(symbol)
    return sorted(set(facts))


def extract_asserted_facts(rhs: str) -> list[str]:
    facts = re.findall(r"\(assert\s+\(\s*([A-Za-z_][\w-]*)", rhs)
    return sorted(set(fact for fact in facts if fact not in KEYWORDS))


def extract_string_slot_values(text: str, slot: str) -> list[str]:
    pattern = r"\(" + re.escape(slot) + r'\s+"([^"]*)"'
    return sorted(set(re.findall(pattern, text)))


def parse_clp(path: Path) -> tuple[list[TemplateInfo], list[RuleInfo]]:
    raw = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    templates: list[TemplateInfo] = []
    rules: list[RuleInfo] = []
    for form in top_level_forms(raw):
        symbol = head_symbol(form)
        if symbol == "deftemplate":
            match = re.match(r"\(\s*deftemplate\s+([^\s()]+)", form)
            if not match:
                continue
            name = match.group(1)
            slots = sorted(set(re.findall(r"\(\s*slot\s+([^\s()]+)", form)))
            templates.append(TemplateInfo(name=name, path=str(path), slots=slots))
        elif symbol == "defrule":
            match = re.match(r"\(\s*defrule\s+([^\s()]+)", form)
            if not match:
                continue
            name = match.group(1)
            body = form_tail_after_name(form, "defrule", name)
            lhs, sep, rhs = body.partition("=>")
            salience_match = re.search(r"\(\s*salience\s+(-?\d+)", lhs)
            salience = int(salience_match.group(1)) if salience_match else 0
            rules.append(
                RuleInfo(
                    name=name,
                    path=str(path),
                    salience=salience,
                    lhs_facts=extract_first_level_facts(lhs if sep else body),
                    rhs_asserts=extract_asserted_facts(rhs),
                    decisions=extract_string_slot_values(rhs, "decision"),
                    route_targets=extract_string_slot_values(rhs, "route_target"),
                    reason_codes=extract_string_slot_values(rhs, "reason_code"),
                )
            )
    return templates, rules


def parse_flow(path: Path) -> FlowInfo:
    data = json.loads(path.read_text(encoding="utf-8"))
    return FlowInfo(
        flow_id=data.get("flow_id", path.stem),
        path=str(path),
        cpp_role=data.get("cpp_role", ""),
        states=[state.get("id", "") for state in data.get("states", [])],
        forbidden_transitions=data.get("forbidden_transitions", []),
    )


def build_rule_edges(rules: Iterable[RuleInfo]) -> list[dict]:
    rule_list = list(rules)
    edges: list[dict] = []
    for producer in rule_list:
        produced = set(producer.rhs_asserts)
        if not produced:
            continue
        for consumer in rule_list:
            if producer.name == consumer.name:
                continue
            shared = sorted(produced.intersection(consumer.lhs_facts))
            for fact in shared:
                edges.append({"from": producer.name, "to": consumer.name, "fact": fact})
    return edges


def dot_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_rule_dot(path: Path, rules: list[RuleInfo], edges: list[dict]) -> None:
    lines = [
        "digraph clips_rules {",
        "  rankdir=TB;",
        '  node [shape=box, fontname="Microsoft YaHei", fontsize=10];',
    ]
    for rule in rules:
        label = f"{rule.name}\\nsalience={rule.salience}"
        if rule.reason_codes:
            label += "\\n" + ",".join(rule.reason_codes[:2])
        lines.append(f'  "{dot_escape(rule.name)}" [label="{dot_escape(label)}"];')
    for edge in edges:
        lines.append(
            f'  "{dot_escape(edge["from"])}" -> "{dot_escape(edge["to"])}" '
            f'[label="{dot_escape(edge["fact"])}"];'
        )
    lines.append("}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_rule_mermaid(path: Path, rules: list[RuleInfo], edges: list[dict]) -> None:
    lines = ["flowchart TB"]
    for rule in rules:
        safe_id = re.sub(r"[^A-Za-z0-9_]", "_", rule.name)
        lines.append(f'  {safe_id}["{rule.name}<br/>salience={rule.salience}"]')
    for edge in edges:
        source = re.sub(r"[^A-Za-z0-9_]", "_", edge["from"])
        target = re.sub(r"[^A-Za-z0-9_]", "_", edge["to"])
        lines.append(f'  {source} -->|"{edge["fact"]}"| {target}')
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_flow_graphs(out_dir: Path, flows: list[FlowInfo]) -> None:
    for flow in flows:
        dot_lines = [
            "digraph mcp_flow {",
            "  rankdir=TB;",
            '  node [shape=box, fontname="Microsoft YaHei", fontsize=10];',
        ]
        mermaid_lines = ["flowchart TB"]
        for state in flow.states:
            dot_lines.append(f'  "{dot_escape(state)}";')
            mermaid_lines.append(f'  {re.sub(r"[^A-Za-z0-9_]", "_", state)}["{state}"]')
        for left, right in zip(flow.states, flow.states[1:]):
            dot_lines.append(f'  "{dot_escape(left)}" -> "{dot_escape(right)}";')
            mermaid_lines.append(
                f'  {re.sub(r"[^A-Za-z0-9_]", "_", left)} --> '
                f'{re.sub(r"[^A-Za-z0-9_]", "_", right)}'
            )
        dot_lines.append("}")
        (out_dir / f"{flow.flow_id}.dot").write_text("\n".join(dot_lines) + "\n", encoding="utf-8")
        (out_dir / f"{flow.flow_id}.mmd").write_text("\n".join(mermaid_lines) + "\n", encoding="utf-8")


def find_conflicts(rules: list[RuleInfo]) -> list[dict]:
    conflicts: list[dict] = []
    for index, left in enumerate(rules):
        for right in rules[index + 1 :]:
            overlap = sorted(set(left.lhs_facts).intersection(right.lhs_facts))
            if not overlap or left.salience != right.salience:
                continue
            left_decision = ",".join(left.decisions)
            right_decision = ",".join(right.decisions)
            if left_decision != right_decision or set(left.route_targets) != set(right.route_targets):
                conflicts.append(
                    {
                        "left": left.name,
                        "right": right.name,
                        "salience": left.salience,
                        "overlap_facts": overlap,
                        "left_decision": left_decision,
                        "right_decision": right_decision,
                    }
                )
    return conflicts


def write_report(
    path: Path,
    templates: list[TemplateInfo],
    rules: list[RuleInfo],
    flows: list[FlowInfo],
    edges: list[dict],
    conflicts: list[dict],
) -> None:
    template_names = {template.name for template in templates}
    lhs_facts = {fact for rule in rules for fact in rule.lhs_facts}
    rhs_facts = {fact for rule in rules for fact in rule.rhs_asserts}
    incoming = {edge["to"] for edge in edges}
    outgoing = {edge["from"] for edge in edges}
    entry_rules = [rule.name for rule in rules if rule.name not in incoming]
    exit_rules = [rule.name for rule in rules if rule.name not in outgoing]
    dead_templates = sorted(template_names - lhs_facts - rhs_facts)
    orphan_lhs = sorted(lhs_facts - rhs_facts - template_names)

    lines = [
        "# CLIPS Rule Observability Report",
        "",
        "## Summary",
        "",
        f"- Templates: {len(templates)}",
        f"- Rules: {len(rules)}",
        f"- Dependency edges: {len(edges)}",
        f"- Flow scripts: {len(flows)}",
        f"- Entry rules: {len(entry_rules)}",
        f"- Exit rules: {len(exit_rules)}",
        f"- Same-salience conflict candidates: {len(conflicts)}",
        "",
        "## Flow Scripts",
        "",
    ]
    if flows:
        lines.extend(["| Flow | States | C++ Role |", "|---|---:|---|"])
        for flow in flows:
            lines.append(f"| `{flow.flow_id}` | {len(flow.states)} | `{flow.cpp_role}` |")
    else:
        lines.append("- None")
    lines.extend(["", "## Dead Or Unused Templates", ""])
    lines.extend([f"- `{name}`" for name in dead_templates] or ["- None"])
    lines.extend(["", "## LHS Facts Without Template Or Producer", ""])
    lines.extend([f"- `{name}`" for name in orphan_lhs] or ["- None"])
    lines.extend(["", "## Conflict Candidates", ""])
    if conflicts:
        lines.extend(["| Left | Right | Salience | Overlap |", "|---|---|---:|---|"])
        for conflict in conflicts[:50]:
            lines.append(
                f"| `{conflict['left']}` | `{conflict['right']}` | "
                f"{conflict['salience']} | `{','.join(conflict['overlap_facts'])}` |"
            )
    else:
        lines.append("- None")
    lines.extend(["", "## Key Continuation Rules", ""])
    for rule in rules:
        if any(code for code in rule.reason_codes if "incomplete" in code or "not_terminal" in code):
            lines.append(
                f"- `{rule.name}`: facts `{','.join(rule.lhs_facts)}` -> "
                f"asserts `{','.join(rule.rhs_asserts)}`; reasons `{','.join(rule.reason_codes)}`"
            )
    lines.extend(["", "## Artifacts", ""])
    lines.extend(
        [
            "- `rules_parse.json`",
            "- `rules_graph.dot`",
            "- `rules_graph.mmd`",
            "- `<flow_id>.dot`",
            "- `<flow_id>.mmd`",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Observe CLIPS rules and MCP flow scripts.")
    parser.add_argument("--rule-root", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()

    rule_root = args.rule_root.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    templates: list[TemplateInfo] = []
    rules: list[RuleInfo] = []
    for path in sorted(rule_root.rglob("*.clp")):
        parsed_templates, parsed_rules = parse_clp(path)
        templates.extend(parsed_templates)
        rules.extend(parsed_rules)

    flows = [parse_flow(path) for path in sorted(rule_root.rglob("*.flow.json"))]
    edges = build_rule_edges(rules)
    conflicts = find_conflicts(rules)

    payload = {
        "rule_root": str(rule_root),
        "templates": [asdict(item) for item in templates],
        "rules": [asdict(item) for item in rules],
        "flows": [asdict(item) for item in flows],
        "dependency_edges": edges,
        "conflict_candidates": conflicts,
    }
    (out_dir / "rules_parse.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_rule_dot(out_dir / "rules_graph.dot", rules, edges)
    write_rule_mermaid(out_dir / "rules_graph.mmd", rules, edges)
    write_flow_graphs(out_dir, flows)
    write_report(out_dir / "rules_impact_report.md", templates, rules, flows, edges, conflicts)

    print(json.dumps({
        "status": "success",
        "rule_root": str(rule_root),
        "out_dir": str(out_dir),
        "template_count": len(templates),
        "rule_count": len(rules),
        "edge_count": len(edges),
        "flow_count": len(flows),
        "conflict_candidate_count": len(conflicts),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
