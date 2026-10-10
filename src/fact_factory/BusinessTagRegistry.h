#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fact_factory {

enum class BusinessTagGroup {
    Intent,
    RequestType,
    SafetyRisk,
    Decision,
    Verification,
    ExecutionClass,
    ActionVerb,
};

struct BusinessTagEntry {
    std::string tag;
    BusinessTagGroup group;
    std::vector<std::string> aliases;
};

inline const std::vector<BusinessTagEntry> & BusinessTagCatalog() {
    static const std::vector<BusinessTagEntry> entries = {
        {"comment_cleanup",       BusinessTagGroup::Intent,      {"删除注释","清理注释","去除注释","移除注释","删注释","删掉注释","删除代码注释"}},
        {"code_format",           BusinessTagGroup::Intent,      {"格式化代码","代码格式化","删除多余回车换行","清理空白","格式化"}},
        {"source_edit",           BusinessTagGroup::Intent,      {"源码编辑","修改代码","编辑代码","修改文件"}},
        {"localized_edit",        BusinessTagGroup::Intent,      {"局部编辑","局部修改","局部编辑修改"}},
        {"source_edit_planning",  BusinessTagGroup::Intent,      {"编辑规划","源码编辑规划","修改规划"}},
        {"code_search",           BusinessTagGroup::Intent,      {"代码搜索","搜索代码","代码查找","查找代码"}},
        {"reindex_preparation",   BusinessTagGroup::Intent,      {"重建索引","索引准备","重新索引"}},
        {"subdirectory_search",   BusinessTagGroup::Intent,      {"子目录搜索","目录搜索","子目录查找"}},
        {"refactor_file",         BusinessTagGroup::Intent,      {"文件重构","重构文件","代码重构"}},
        {"text_cleaning",         BusinessTagGroup::Intent,      {"文本清理","清理文本","文本清洗"}},
        {"analysis_review",       BusinessTagGroup::RequestType,  {"分析评审","分析审查","评审分析"}},
        {"read_observe",          BusinessTagGroup::RequestType,  {"只读观察","读取观察","观察读取"}},
        {"file_mutation",         BusinessTagGroup::RequestType,  {"文件变更","文件修改","文件改动"}},
        {"state_mutation",        BusinessTagGroup::RequestType,  {"状态变更","状态修改","状态改动"}},
        {"execution_task",        BusinessTagGroup::RequestType,  {"执行任务","任务执行"}},
        {"cxparser_flow_execution",BusinessTagGroup::RequestType, {"解析流程执行","解析流程","cxparser流程"}},
        {"rag_clips_run",         BusinessTagGroup::RequestType,  {"RAG运行","rag运行","clips运行"}},
        {"clips_control",         BusinessTagGroup::RequestType,  {"规则控制","CLIPS控制","clips控制"}},
        {"write_audited",         BusinessTagGroup::SafetyRisk,   {"写操作审计","审计写操作","写审计"}},
        {"read_only",             BusinessTagGroup::SafetyRisk,   {"只读","仅读"}},
        {"low",                   BusinessTagGroup::SafetyRisk,   {"低风险","低","低危"}},
        {"high",                  BusinessTagGroup::SafetyRisk,   {"高风险","高","高危"}},
        {"allow",                 BusinessTagGroup::Decision,     {"放行","允许","通过"}},
        {"block",                 BusinessTagGroup::Decision,     {"阻断","阻止","拒绝","禁止"}},
        {"route",                 BusinessTagGroup::Decision,     {"路由","重定向","转发"}},
        {"verified",              BusinessTagGroup::Verification,  {"已验证","验证通过","确认"}},
        {"not_verified",          BusinessTagGroup::Verification,  {"未验证","未确认","待验证"}},
        {"invalid",               BusinessTagGroup::Verification,  {"无效","非法","不合法"}},
        {"read",                  BusinessTagGroup::ExecutionClass,{"读","读取"}},
        {"write",                 BusinessTagGroup::ExecutionClass,{"写","写入"}},
        {"execute",               BusinessTagGroup::ExecutionClass,{"执行","运行"}},
        {"probe",                 BusinessTagGroup::ActionVerb,    {"探测","探查","探针"}},
        {"scan",                  BusinessTagGroup::ActionVerb,    {"扫描","扫查"}},
        {"delete",                BusinessTagGroup::ActionVerb,    {"删除","删","去掉","移除"}},
        {"insert",                BusinessTagGroup::ActionVerb,    {"插入","添加","新增"}},
        {"replace",               BusinessTagGroup::ActionVerb,    {"替换","取代","更换"}},
        {"apply",                 BusinessTagGroup::ActionVerb,    {"应用","施加","生效"}},
        {"preview",               BusinessTagGroup::ActionVerb,    {"预览","预检","预看"}},
        {"build",                 BusinessTagGroup::ActionVerb,    {"构建","编译","编"}},
        {"test",                  BusinessTagGroup::ActionVerb,    {"测试","测验","检测"}},
    };
    return entries;
}

inline const std::unordered_set<std::string> & BusinessTagWhitelist() {
    static const std::unordered_set<std::string> set = []() {
        std::unordered_set<std::string> s;
        for (const auto & entry : BusinessTagCatalog()) {
            s.insert(entry.tag);
        }
        return s;
    }();
    return set;
}

inline const std::unordered_map<std::string, std::string> & BusinessAliasMap() {
    static const std::unordered_map<std::string, std::string> map = []() {
        std::unordered_map<std::string, std::string> m;
        for (const auto & entry : BusinessTagCatalog()) {
            m[entry.tag] = entry.tag;
            for (const auto & alias : entry.aliases) {
                m[alias] = entry.tag;
            }
        }
        return m;
    }();
    return map;
}

inline bool IsBusinessTag(std::string_view value) {
    return BusinessTagWhitelist().count(std::string(value)) > 0;
}

inline std::string ResolveBusinessTag(std::string_view alias) {
    const auto & map = BusinessAliasMap();
    auto it = map.find(std::string(alias));
    if (it == map.end()) {
        return std::string();
    }
    return it->second;
}

}
