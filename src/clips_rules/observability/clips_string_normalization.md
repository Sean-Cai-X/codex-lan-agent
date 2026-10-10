# CLIPS string normalization

CLIPS facts must use normalized ASCII tokens instead of raw user text, Windows paths,
or nested JSON. The original value stays in MCP result fields, artifact files, or refs.

## Character table

| Input | CLIPS token |
|---|---|
| `A-Z` | lowercase `a-z` |
| `a-z`, `0-9` | unchanged |
| `\` | `/` |
| space, tab | `_` |
| newline | `_nl_` |
| carriage return | `_cr_` |
| `"` | `_dq_` |
| `'` | `_sq_` |
| `{` | `_lb_` |
| `}` | `_rb_` |
| `[` | `_ls_` |
| `]` | `_rs_` |
| `(` | `_lp_` |
| `)` | `_rp_` |
| `,` | `_cm_` |
| `;` | `_sc_` |
| `=` | `_eq_` |
| `&` | `_and_` |
| `|` | `_or_` |
| `*` | `_star_` |
| `?` | `_q_` |
| `#` | `_hash_` |
| `%` | `_pct_` |
| other bytes | `_xNN_` |

## Length rule

Normalized values longer than 160 characters are truncated to the first 120
characters and suffixed with `_h_<stable_hash>`.

## Routing rule

CLIPS rules should match canonical tokens such as `comment_cleanup`,
`code_format`, tool names, status flags, and reason codes. They should not match
raw Chinese text, raw Windows paths, or inline JSON.
