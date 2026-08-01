#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/Analyses/Dominators.h>
#include <clang/Analysis/CFG.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Index/USRGeneration.h>

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <set>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class Role {
    none,
    cxx,
    r,
    neutral,
    entrypoint,
    abi_shim,
    trusted_unwind,
    conflicting
};

llvm::cl::OptionCategory lint_category("charr-lint options");

llvm::cl::opt<bool> audit_mode(
    "audit",
    llvm::cl::desc("Report findings without returning a policy failure"),
    llvm::cl::init(false),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<bool> main_files_only(
    "main-files-only",
    llvm::cl::desc("Check definitions written in each translation unit's main file"),
    llvm::cl::init(false),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<bool> dump_external_calls(
    "dump-external-calls",
    llvm::cl::desc("Print the inferred external-effect manifest"),
    llvm::cl::init(false),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<std::string> effects_path(
    "effects",
    llvm::cl::desc("Reviewed external-effect manifest"),
    llvm::cl::value_desc("path"),
    llvm::cl::init(""),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<std::string> effect_overrides_path(
    "effect-overrides",
    llvm::cl::desc("Manual external-effect overrides"),
    llvm::cl::value_desc("path"),
    llvm::cl::init(""),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<std::string> write_effects_manifest(
    "write-effects-manifest",
    llvm::cl::desc("Write the inferred external-effect manifest"),
    llvm::cl::value_desc("path"),
    llvm::cl::init(""),
    llvm::cl::cat(lint_category)
);

llvm::cl::opt<std::string> code_map_dir(
    "code-map-dir",
    llvm::cl::desc("Write code-map tables and browser data to this directory"),
    llvm::cl::value_desc("directory"),
    llvm::cl::init(""),
    llvm::cl::cat(lint_category)
);

struct Effect {
    bool fallible_r = false;
    bool cpp_throw = false;
    bool returns_owner = false;
    bool raw_acquire = false;
    bool raw_release = false;
};

std::string effect_name(const Effect& effect)
{
    std::vector<std::string> components;
    if (effect.fallible_r)
        components.push_back("r");
    if (effect.returns_owner)
        components.push_back("owner");
    else if (effect.cpp_throw)
        components.push_back("cxx");
    if (effect.raw_acquire)
        components.push_back("raw-acquire");
    if (effect.raw_release)
        components.push_back("raw-release");
    if (components.empty())
        return "neutral";
    if (!effect.fallible_r && !effect.cpp_throw &&
            (effect.raw_acquire || effect.raw_release)) {
        components.insert(components.begin(), "neutral");
    }

    std::string result;
    for (const std::string& component : components) {
        if (!result.empty())
            result += '+';
        result += component;
    }
    return result;
}

bool parse_effect(
    llvm::StringRef name, Effect& effect, std::string& error
)
{
    if (name.empty() || name.ends_with("+")) {
        error = "empty effect component";
        return false;
    }

    std::set<std::string> components;
    bool neutral = false;

    while (!name.empty()) {
        const std::pair<llvm::StringRef, llvm::StringRef> split =
            name.split('+');
        const llvm::StringRef component = split.first;
        name = split.second;

        if (component.empty()) {
            error = "empty effect component";
            return false;
        }
        if (!components.insert(component.str()).second) {
            error = "duplicate effect component '" + component.str() + "'";
            return false;
        }

        if (component == "neutral") {
            neutral = true;
        }
        else if (component == "r") {
            effect.fallible_r = true;
        }
        else if (component == "cxx") {
            effect.cpp_throw = true;
        }
        else if (component == "owner") {
            // Cleanup-bearing results use the conservative C++ error policy.
            effect.cpp_throw = true;
            effect.returns_owner = true;
        }
        else if (component == "raw-acquire") {
            effect.raw_acquire = true;
        }
        else if (component == "raw-release") {
            effect.raw_release = true;
        }
        else {
            error = "unknown effect '" + component.str() + "'";
            return false;
        }
    }

    if (components.empty()) {
        error = "empty effect";
        return false;
    }
    if (neutral && (effect.fallible_r || effect.cpp_throw)) {
        error = "'neutral' cannot be combined with 'r', 'cxx', or 'owner'";
        return false;
    }

    return true;
}

bool parse_effect_delta(
    llvm::StringRef name, Effect& effect, std::string& error
)
{
    if (name.empty()) {
        return true;
    }
    if (!parse_effect(name, effect, error))
        return false;
    if (name.split('+').first == "neutral" || name.contains("+neutral")) {
        error = "'neutral' is not an override component";
        return false;
    }
    return true;
}

bool effect_contains(const Effect& whole, const Effect& part)
{
    return (!part.fallible_r || whole.fallible_r) &&
        (!part.cpp_throw || whole.cpp_throw) &&
        (!part.returns_owner || whole.returns_owner) &&
        (!part.raw_acquire || whole.raw_acquire) &&
        (!part.raw_release || whole.raw_release);
}

bool has_effect(const Effect& effect)
{
    return effect.fallible_r || effect.cpp_throw || effect.returns_owner ||
        effect.raw_acquire || effect.raw_release;
}

bool effects_overlap(const Effect& left, const Effect& right)
{
    return (left.fallible_r && right.fallible_r) ||
        (left.cpp_throw && right.cpp_throw) ||
        (left.returns_owner && right.returns_owner) ||
        (left.raw_acquire && right.raw_acquire) ||
        (left.raw_release && right.raw_release);
}

bool adds_existing_effect(const Effect& whole, const Effect& addition)
{
    return (addition.fallible_r && whole.fallible_r) ||
        (addition.returns_owner && whole.returns_owner) ||
        (addition.cpp_throw && !addition.returns_owner && whole.cpp_throw) ||
        (addition.raw_acquire && whole.raw_acquire) ||
        (addition.raw_release && whole.raw_release);
}

void add_effect(Effect& target, const Effect& addition)
{
    target.fallible_r = target.fallible_r || addition.fallible_r;
    target.cpp_throw = target.cpp_throw || addition.cpp_throw;
    target.returns_owner = target.returns_owner || addition.returns_owner;
    target.raw_acquire = target.raw_acquire || addition.raw_acquire;
    target.raw_release = target.raw_release || addition.raw_release;
}

void remove_effect(Effect& target, const Effect& removal)
{
    if (removal.fallible_r)
        target.fallible_r = false;
    if (removal.cpp_throw)
        target.cpp_throw = false;
    if (removal.returns_owner)
        target.returns_owner = false;
    if (removal.raw_acquire)
        target.raw_acquire = false;
    if (removal.raw_release)
        target.raw_release = false;
}

std::string function_key(const clang::FunctionDecl& function)
{
    return function.getQualifiedNameAsString() + "\t" +
        function.getType().getCanonicalType().getAsString();
}

std::string function_usr(const clang::FunctionDecl& function)
{
    llvm::SmallString<256> usr;
    if (!clang::index::generateUSRForDecl(&function, usr) && !usr.empty())
        return usr.str().str();
    return "";
}

std::vector<std::string> split_tsv(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

struct EffectOverride {
    Effect add;
    Effect remove;
    std::string reason;
};

struct ManifestEntry {
    std::string effect;
    std::string inferred_effect;
    std::string inference_basis;
    std::string override_reason;
};

struct ResolvedExternalEffect {
    Effect inferred;
    Effect effective;
    std::string inferred_name;
    std::string effective_name;
    std::string inference_basis;
    std::string override_reason;
    bool manifest_present = false;
    bool manifest_matches = false;
    bool integrity_problem = false;
    std::string manifest_problem;
};

bool is_nothrow(const clang::FunctionDecl& function);
bool returns_owner_value(const clang::FunctionDecl& function);
bool constructs_owner_value(const clang::FunctionDecl& function);
bool is_fallible_r_name(llvm::StringRef qualified_name);
bool is_r_api_declaration(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
);

class ExternalEffects {
private:
    std::map<std::string, ManifestEntry> manifest_;
    std::map<std::string, EffectOverride> overrides_;

public:
    bool load_manifest(llvm::StringRef path, std::string& error)
    {
        if (path.empty())
            return true;

        std::ifstream input(path.str());
        if (!input) {
            error = "cannot open external effect manifest: " + path.str();
            return false;
        }

        std::string line;
        unsigned line_number = 0;
        bool saw_header = false;
        while (std::getline(input, line)) {
            ++line_number;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            if (!saw_header) {
                static const std::string expected_header =
                    "effect\tqualified_name\tcanonical_type\t"
                    "inferred_effect\tinference_basis\toverride_reason";
                if (line != expected_header) {
                    error = path.str() + ":" +
                        std::to_string(line_number) +
                        ": invalid external effect manifest header";
                    return false;
                }
                saw_header = true;
                continue;
            }

            const std::vector<std::string> fields = split_tsv(line);
            if (fields.size() != 6) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": expected six tab-separated fields";
                return false;
            }

            Effect effect;
            std::string effect_error;
            if (!parse_effect(fields[0], effect, effect_error)) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": " + effect_error;
                return false;
            }

            if (fields[3].empty() !=
                    (fields[4] == "legacy-unobserved")) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": an empty inferred effect requires "
                    "'legacy-unobserved' provenance";
                return false;
            }
            if (!fields[3].empty()) {
                Effect inferred;
                if (!parse_effect(fields[3], inferred, effect_error)) {
                    error = path.str() + ":" +
                        std::to_string(line_number) + ": " + effect_error;
                    return false;
                }
            }

            const std::string key = fields[1] + "\t" + fields[2];
            const ManifestEntry entry{
                effect_name(effect), fields[3], fields[4], fields[5]
            };
            if (!manifest_.insert(std::make_pair(key, entry)).second) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": duplicate manifest entry";
                return false;
            }
        }
        if (!saw_header) {
            error = path.str() + ": missing external effect manifest header";
            return false;
        }
        return true;
    }

    bool load_overrides(llvm::StringRef path, std::string& error)
    {
        if (path.empty())
            return true;

        std::ifstream input(path.str());
        if (!input) {
            error = "cannot open external effect overrides: " + path.str();
            return false;
        }

        std::string line;
        unsigned line_number = 0;
        bool saw_header = false;
        while (std::getline(input, line)) {
            ++line_number;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            if (!saw_header) {
                static const std::string expected_header =
                    "add_effects\tremove_effects\tqualified_name\t"
                    "canonical_type\treason";
                if (line != expected_header) {
                    error = path.str() + ":" +
                        std::to_string(line_number) +
                        ": invalid external effect override header";
                    return false;
                }
                saw_header = true;
                continue;
            }

            const std::vector<std::string> fields = split_tsv(line);
            if (fields.size() != 5) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": expected five tab-separated fields";
                return false;
            }
            if (fields[0].empty() && fields[1].empty()) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": override must add or remove an effect";
                return false;
            }
            if (fields[4].empty()) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": override requires a reason";
                return false;
            }

            EffectOverride entry;
            std::string effect_error;
            if (!parse_effect_delta(fields[0], entry.add, effect_error) ||
                    !parse_effect_delta(
                        fields[1], entry.remove, effect_error)) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": " + effect_error;
                return false;
            }
            if (effects_overlap(entry.add, entry.remove)) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": the same effect cannot be added and removed";
                return false;
            }
            if (entry.remove.returns_owner) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": ownership inference cannot be removed";
                return false;
            }
            entry.reason = fields[4];

            const std::string key = fields[2] + "\t" + fields[3];
            if (!overrides_.insert(std::make_pair(key, entry)).second) {
                error = path.str() + ":" + std::to_string(line_number) +
                    ": duplicate override entry";
                return false;
            }
        }
        if (!saw_header) {
            error = path.str() + ": missing external effect override header";
            return false;
        }

        for (const auto& item : overrides_) {
            const auto manifest = manifest_.find(item.first);
            if (manifest == manifest_.end()) {
                error = path.str() + ": override for '" + item.first +
                    "' has no reviewed manifest entry";
                return false;
            }

            if (manifest->second.inference_basis == "legacy-unobserved") {
                Effect effective;
                std::string effect_error;
                if (!parse_effect(
                        manifest->second.effect, effective, effect_error)) {
                    error = path.str() + ": legacy manifest entry for '" +
                        item.first + "' has an invalid effect: " +
                        effect_error;
                    return false;
                }
                add_effect(effective, item.second.add);
                remove_effect(effective, item.second.remove);
                manifest->second.effect = effect_name(effective);
                manifest->second.override_reason = item.second.reason;
            }
        }
        return true;
    }

    ResolvedExternalEffect resolve(
        const clang::FunctionDecl& function,
        const clang::SourceManager& source_manager
    ) const;

    const std::map<std::string, ManifestEntry>& manifest() const noexcept
    {
        return manifest_;
    }
};

const char* role_name(Role role)
{
    switch (role) {
    case Role::cxx: return "C++ helper";
    case Role::r: return "R helper";
    case Role::neutral: return "neutral helper";
    case Role::entrypoint: return "entry point";
    case Role::abi_shim: return "ABI shim";
    case Role::trusted_unwind: return "trusted unwind intrinsic";
    case Role::conflicting: return "conflicting roles";
    case Role::none: return "unclassified";
    }
    return "unknown";
}

const char* role_tag(Role role)
{
    switch (role) {
    case Role::cxx: return "cxx_helper";
    case Role::r: return "r_helper";
    case Role::neutral: return "neutral_helper";
    case Role::entrypoint: return "entrypoint";
    case Role::abi_shim: return "abi_shim";
    case Role::trusted_unwind: return "trusted_unwind";
    case Role::conflicting: return "conflicting";
    case Role::none: return "unclassified";
    }
    return "unknown";
}

Role annotation_role(const clang::FunctionDecl& function)
{
    Role found = Role::none;
    for (const clang::FunctionDecl* redecl : function.redecls()) {
        for (const clang::AnnotateAttr* attr :
                redecl->specific_attrs<clang::AnnotateAttr>()) {
            Role current = Role::none;
            const llvm::StringRef value = attr->getAnnotation();
            if (value == "charr.cxx_helper")
                current = Role::cxx;
            else if (value == "charr.r_helper")
                current = Role::r;
            else if (value == "charr.neutral_helper")
                current = Role::neutral;
            else if (value == "charr.entrypoint")
                current = Role::entrypoint;
            else if (value == "charr.abi_shim")
                current = Role::abi_shim;
            else if (value == "charr.trusted_unwind")
                current = Role::trusted_unwind;

            if (current == Role::none)
                continue;
            if (found != Role::none && found != current)
                return Role::conflicting;
            found = current;
        }
    }
    if (found == Role::none) {
        const clang::FunctionDecl* pattern =
            function.getTemplateInstantiationPattern(false);
        if (pattern != nullptr && pattern != &function)
            return annotation_role(*pattern);
    }
    return found;
}

bool is_implicit_trivial_special_member(
    const clang::FunctionDecl& function
) {
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
    if (method == nullptr || !method->isImplicit())
        return false;

    const clang::CXXRecordDecl* record = method->getParent();
    if (const auto* constructor =
            llvm::dyn_cast<clang::CXXConstructorDecl>(method)) {
        if (constructor->isDefaultConstructor())
            return record->hasTrivialDefaultConstructor();
        if (constructor->isCopyConstructor())
            return record->hasTrivialCopyConstructor();
        if (constructor->isMoveConstructor())
            return record->hasTrivialMoveConstructor();
        return false;
    }
    if (llvm::isa<clang::CXXDestructorDecl>(method))
        return record->hasTrivialDestructor();
    if (method->isCopyAssignmentOperator())
        return record->hasTrivialCopyAssignment();
    if (method->isMoveAssignmentOperator())
        return record->hasTrivialMoveAssignment();
    return false;
}

bool is_nothrow(const clang::FunctionDecl& function)
{
    const clang::FunctionProtoType* prototype =
        function.getType()->getAs<clang::FunctionProtoType>();
    return prototype != nullptr && prototype->isNothrow();
}

bool is_fallible_r_name(llvm::StringRef qualified_name)
{
    const std::pair<llvm::StringRef, llvm::StringRef> split =
        qualified_name.rsplit("::");
    const llvm::StringRef name = split.second.empty()
        ? split.first
        : split.second;
    if (name.starts_with("Rf_") || name.starts_with("R_"))
        return true;

    return name == "LENGTH" || name == "XLENGTH" ||
        name == "STRING_ELT" || name == "VECTOR_ELT" ||
        name == "SET_STRING_ELT" || name == "SET_VECTOR_ELT" ||
        name == "CHAR" || name == "STRING_PTR_RO";
}

bool is_owner_type(clang::QualType type)
{
    type = type.getNonReferenceType();
    const clang::RecordType* record = type->getAs<clang::RecordType>();
    if (record == nullptr)
        return false;
    for (const clang::AnnotateAttr* attr :
            record->getDecl()->specific_attrs<clang::AnnotateAttr>()) {
        if (attr->getAnnotation() == "charr.owner_type")
            return true;
    }
    return false;
}

bool is_cleanup_bearing_value(clang::QualType type)
{
    if (type->isVoidType() || type->isPointerType() ||
            type->isReferenceType()) {
        return false;
    }

    return type.isDestructedType() != clang::QualType::DK_none ||
        is_owner_type(type);
}

bool returns_owner_value(const clang::FunctionDecl& function)
{
    return is_cleanup_bearing_value(function.getReturnType());
}

bool constructs_owner_value(const clang::FunctionDecl& function)
{
    const auto* constructor =
        llvm::dyn_cast<clang::CXXConstructorDecl>(&function);
    if (constructor == nullptr)
        return false;
    return is_cleanup_bearing_value(
        function.getASTContext().getRecordType(constructor->getParent())
    );
}

bool is_r_header_path(llvm::StringRef raw_path)
{
    std::string normalized = raw_path.str();
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const llvm::StringRef path(normalized);
    const llvm::StringRef filename = llvm::sys::path::filename(path);
    return path.contains("/R/include/") ||
        path.contains("/R.framework/Headers/") ||
        path.contains("/R.framework/Resources/include/") ||
        path.contains("/R_ext/") || filename == "R.h" ||
        filename == "Rdefines.h" || filename == "Rinternals.h" ||
        filename == "Rinlinedfuns.h" || filename == "Rmath.h";
}

// A declaration whose name is produced by token pasting spells in Clang's
// scratch buffer, which belongs to no header. ICU renames every C entry point
// that way, so header classification has to consider the macro invocation
// site too.
std::vector<clang::SourceLocation> declaring_file_locations(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
)
{
    std::vector<clang::SourceLocation> locations;
    for (const clang::FunctionDecl* redecl : function.redecls()) {
        const clang::SourceLocation written = redecl->getLocation();
        for (const clang::SourceLocation location : {
                source_manager.getSpellingLoc(written),
                source_manager.getExpansionLoc(written)
            }) {
            if (location.isValid())
                locations.push_back(location);
        }
    }
    return locations;
}

bool is_r_api_declaration(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
)
{
    for (const clang::SourceLocation location :
            declaring_file_locations(function, source_manager)) {
        if (is_r_header_path(source_manager.getFilename(location)))
            return true;
    }
    return false;
}

std::string join_basis(const std::vector<std::string>& components)
{
    std::string result;
    for (const std::string& component : components) {
        if (!result.empty())
            result += '+';
        result += component;
    }
    return result;
}

bool has_c_language_linkage(const clang::FunctionDecl& function)
{
    for (const clang::FunctionDecl* redecl : function.redecls()) {
        if (redecl->isExternC())
            return true;
    }
    return false;
}

bool is_reviewed_c_api_declaration(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
)
{
    if (!has_c_language_linkage(function))
        return false;

    for (const clang::SourceLocation location :
            declaring_file_locations(function, source_manager)) {
        if (source_manager.isInSystemHeader(location) ||
                is_r_header_path(source_manager.getFilename(location))) {
            return true;
        }

        std::string normalized =
            source_manager.getFilename(location).str();
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const llvm::StringRef path(normalized);
        if (path.contains("/src/icu78/unicode/") ||
                path.contains("/charport/") ||
                llvm::sys::path::filename(path) == "charport.h") {
            return true;
        }
    }
    return false;
}

ResolvedExternalEffect ExternalEffects::resolve(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
) const {
    ResolvedExternalEffect result;
    std::vector<std::string> basis;

    if (is_r_api_declaration(function, source_manager)) {
        result.inferred.fallible_r = true;
        basis.push_back("rule:r-header");
    }
    else if (is_fallible_r_name(function.getQualifiedNameAsString())) {
        result.inferred.fallible_r = true;
        basis.push_back("rule:r-name");
    }

    if (returns_owner_value(function)) {
        result.inferred.cpp_throw = true;
        result.inferred.returns_owner = true;
        basis.push_back("clang:cleanup-return");
    }
    else if (constructs_owner_value(function)) {
        result.inferred.cpp_throw = true;
        result.inferred.returns_owner = true;
        basis.push_back("clang:cleanup-construction");
    }
    else if (is_nothrow(function)) {
        basis.push_back("clang:no-cxx-propagation");
    }
    else if (is_reviewed_c_api_declaration(function, source_manager)) {
        basis.push_back("rule:reviewed-c-api");
    }
    else {
        result.inferred.cpp_throw = true;
        basis.push_back("clang:not-noexcept");
    }

    result.inferred_name = effect_name(result.inferred);
    result.inference_basis = join_basis(basis);
    result.effective = result.inferred;

    const std::string key = function_key(function);
    const auto override = overrides_.find(key);
    if (override != overrides_.end()) {
        result.override_reason = override->second.reason;
        if (has_effect(override->second.add) &&
                adds_existing_effect(
                    result.inferred, override->second.add)) {
            result.integrity_problem = true;
            result.manifest_problem =
                "override redundantly adds an inferred effect";
        }
        else if (has_effect(override->second.remove) &&
                !effect_contains(
                    result.inferred, override->second.remove)) {
            result.integrity_problem = true;
            result.manifest_problem =
                "override removes an effect that was not inferred";
        }
        else if (result.inferred.returns_owner &&
                override->second.remove.cpp_throw) {
            result.integrity_problem = true;
            result.manifest_problem =
                "override cannot remove the C++ effect implied by ownership";
        }
        else {
            add_effect(result.effective, override->second.add);
            remove_effect(result.effective, override->second.remove);
        }
    }
    result.effective_name = effect_name(result.effective);

    const auto manifest = manifest_.find(key);
    if (manifest == manifest_.end())
        return result;
    result.manifest_present = true;

    if (result.manifest_problem.empty() &&
            manifest->second.effect == result.effective_name &&
            manifest->second.inferred_effect == result.inferred_name &&
            manifest->second.inference_basis == result.inference_basis &&
            manifest->second.override_reason == result.override_reason) {
        result.manifest_matches = true;
        return result;
    }

    if (result.manifest_problem.empty()) {
        result.manifest_problem = "recorded effect '" +
            manifest->second.effect + "'; resolved effect '" +
            result.effective_name + "' from inferred effect '" +
            result.inferred_name + "' (" + result.inference_basis + ")";
        if (!result.override_reason.empty()) {
            result.manifest_problem += " with override '" +
                result.override_reason + "'";
        }
    }
    return result;
}

bool is_charr_owned_path(llvm::StringRef path)
{
    if (path.contains("/src/icu78/") || path.starts_with("src/icu78/"))
        return false;

    return path.contains("/src/") || path.starts_with("src/");
}

bool is_charr_owned(
    const clang::FunctionDecl& function,
    const clang::SourceManager& source_manager
)
{
    const auto is_owned_location = [&](clang::SourceLocation location) {
        if (location.isInvalid())
            return false;
        if (source_manager.isWrittenInMainFile(location))
            return true;
        return is_charr_owned_path(source_manager.getFilename(location));
    };

    const clang::SourceLocation spelling =
        source_manager.getSpellingLoc(function.getLocation());
    if (is_owned_location(spelling))
        return true;

    // Token-pasted wrapper names spell in Clang's scratch buffer. Attribute
    // ownership to the macro invocation that produced the declaration.
    return is_owned_location(
        source_manager.getExpansionLoc(function.getLocation())
    );
}

class Reporter {
private:
    unsigned errors_ = 0;
    unsigned integrity_errors_ = 0;
    std::set<std::string> emitted_;
    std::map<std::string, ManifestEntry> external_calls_;
    std::map<std::string, std::string> external_usrs_;

    static void write_manifest_row(
        llvm::raw_ostream& output,
        const std::string& key,
        const ManifestEntry& entry
    ) {
        const std::size_t tab = key.find('\t');
        output << entry.effect << '\t' << key.substr(0, tab) << '\t'
               << key.substr(tab + 1) << '\t' << entry.inferred_effect
               << '\t' << entry.inference_basis << '\t'
               << entry.override_reason << '\n';
    }

    void emit(
        const clang::SourceManager& source_manager,
        clang::SourceLocation location,
        llvm::StringRef message,
        bool integrity
    ) {
        const clang::PresumedLoc presumed =
            source_manager.getPresumedLoc(location);
        std::string key;
        llvm::raw_string_ostream key_stream(key);
        if (presumed.isValid()) {
            key_stream << presumed.getFilename() << ':'
                       << presumed.getLine() << ':'
                       << presumed.getColumn() << ':';
        }
        key_stream << message;
        key_stream.flush();
        if (!emitted_.insert(key).second)
            return;

        ++errors_;
        if (integrity)
            ++integrity_errors_;
        if (presumed.isValid()) {
            llvm::errs() << presumed.getFilename() << ':'
                         << presumed.getLine() << ':'
                         << presumed.getColumn() << ": error: ";
        }
        else {
            llvm::errs() << "error: ";
        }
        llvm::errs() << message << '\n';
    }

public:
    void error(
        const clang::SourceManager& source_manager,
        clang::SourceLocation location,
        llvm::StringRef message
    ) {
        emit(source_manager, location, message, false);
    }

    void integrity_error(
        const clang::SourceManager& source_manager,
        clang::SourceLocation location,
        llvm::StringRef message
    ) {
        emit(source_manager, location, message, true);
    }

    unsigned errors() const noexcept
    {
        return errors_;
    }

    unsigned integrity_errors() const noexcept
    {
        return integrity_errors_;
    }

    void external_call(
        const clang::FunctionDecl& function,
        const ResolvedExternalEffect& resolved,
        const clang::SourceManager& source_manager,
        clang::SourceLocation call_location
    ) {
        const std::string key = function_key(function);
        const std::string usr = function_usr(function);
        if (!usr.empty()) {
            const auto inserted_usr = external_usrs_.insert(
                std::make_pair(key, usr)
            );
            if (!inserted_usr.second && inserted_usr.first->second != usr) {
                integrity_error(
                    source_manager, call_location,
                    "external manifest key identifies distinct function "
                    "template specializations"
                );
            }
        }

        const ManifestEntry entry{
            resolved.effective_name,
            resolved.inferred_name,
            resolved.inference_basis,
            resolved.override_reason
        };
        const auto inserted = external_calls_.insert(
            std::make_pair(key, entry)
        );
        if (!inserted.second &&
                (inserted.first->second.effect != entry.effect ||
                 inserted.first->second.inferred_effect !=
                    entry.inferred_effect ||
                 inserted.first->second.inference_basis !=
                    entry.inference_basis ||
                 inserted.first->second.override_reason !=
                    entry.override_reason)) {
            integrity_error(
                source_manager, call_location,
                "external call has conflicting inferred effects across "
                "translation units"
            );
        }
    }

    void print_external_calls() const
    {
        llvm::outs() << "effect\tqualified_name\tcanonical_type\t"
                     << "inferred_effect\tinference_basis\t"
                     << "override_reason\n";
        for (const auto& call : external_calls_)
            write_manifest_row(llvm::outs(), call.first, call.second);
    }

    bool write_external_calls(
        llvm::StringRef path,
        const std::map<std::string, ManifestEntry>& existing,
        std::string& error
    ) const {
        std::map<std::string, ManifestEntry> merged = existing;
        for (const auto& call : external_calls_)
            merged[call.first] = call.second;

        int file_descriptor = -1;
        llvm::SmallString<256> temporary_path;
        std::error_code file_error = llvm::sys::fs::createUniqueFile(
            path.str() + ".tmp-%%%%%%", file_descriptor, temporary_path
        );
        if (file_error) {
            error = "cannot write external effect manifest: " +
                file_error.message();
            return false;
        }

        {
            llvm::raw_fd_ostream output(file_descriptor, true);
            output << "effect\tqualified_name\tcanonical_type\t"
                   << "inferred_effect\tinference_basis\t"
                   << "override_reason\n";
            for (const auto& call : merged)
                write_manifest_row(output, call.first, call.second);
            output.flush();
            if (output.has_error()) {
                error = "cannot write external effect manifest";
                llvm::sys::fs::remove(temporary_path);
                return false;
            }
        }

        file_error = llvm::sys::fs::rename(temporary_path, path);
        if (file_error) {
            llvm::sys::fs::remove(temporary_path);
            error = "cannot replace external effect manifest: " +
                file_error.message();
            return false;
        }
        return true;
    }
};

struct CallRecord {
    const clang::Expr* expression;
    const clang::FunctionDecl* callee;
    Role role;
    bool fallible_r;
    bool cpp_throw;
    bool returns_owner;
    bool raw_acquire;
    bool raw_release;
    bool construction;
    bool constructed_owner;
    bool external_call;
    ResolvedExternalEffect external_effect;
};

const clang::FunctionDecl* resolve_direct_callee(
    const clang::CallExpr& expression
) {
    if (const clang::FunctionDecl* direct = expression.getDirectCallee())
        return direct;

    // Clang leaves calls such as helper<Flag>(concrete_arguments...) unresolved
    // in the primary template. Type-dependent arguments keep ADL open until
    // instantiation, so those calls must continue to fail closed.
    const clang::Expr* callee =
        expression.getCallee()->IgnoreParenImpCasts();
    const auto* lookup =
        llvm::dyn_cast<clang::UnresolvedLookupExpr>(callee);
    if (lookup == nullptr)
        return nullptr;

    if (lookup->requiresADL()) {
        for (const clang::Expr* argument : expression.arguments()) {
            if (argument->isTypeDependent())
                return nullptr;
        }
    }

    const clang::FunctionDecl* resolved = nullptr;
    for (const clang::NamedDecl* candidate : lookup->decls()) {
        const clang::FunctionDecl* function =
            llvm::dyn_cast<clang::FunctionDecl>(candidate);
        if (const auto* function_template =
                llvm::dyn_cast<clang::FunctionTemplateDecl>(candidate)) {
            function = function_template->getTemplatedDecl();
        }
        if (function == nullptr)
            return nullptr;

        function = function->getCanonicalDecl();
        if (resolved != nullptr && resolved != function)
            return nullptr;
        resolved = function;
    }
    return resolved;
}

class BodyVisitor : public clang::RecursiveASTVisitor<BodyVisitor> {
private:
    clang::ASTContext& context_;
    const ExternalEffects& effects_;

public:
    std::vector<const clang::VarDecl*> locals;
    std::vector<const clang::VarDecl*> owners;
    std::vector<const clang::CXXThrowExpr*> throws;
    std::vector<const clang::CXXNewExpr*> allocations;
    std::vector<const clang::CXXDeleteExpr*> deallocations;
    std::vector<const clang::MaterializeTemporaryExpr*> lifetime_owners;
    std::vector<const clang::ReturnStmt*> returns;
    std::vector<const clang::IfStmt*> ifs;
    std::vector<CallRecord> calls;

    BodyVisitor(
        clang::ASTContext& context, const ExternalEffects& effects
    ) : context_(context), effects_(effects) {}

    void record_call(
        clang::Expr* expression,
        const clang::FunctionDecl* callee,
        bool construction,
        bool constructed_owner
    ) {
        if (callee == nullptr) {
            calls.push_back({
                expression, nullptr, Role::none,
                false, true, false, false, false,
                construction, constructed_owner, false, {}
            });
            return;
        }

        Role role = annotation_role(*callee);
        const bool implicit_trivial =
            is_implicit_trivial_special_member(*callee);
        if (role == Role::none && implicit_trivial)
            role = Role::neutral;
        const bool external_call = role == Role::none &&
            !is_charr_owned(*callee, context_.getSourceManager());
        ResolvedExternalEffect external_effect;
        Effect effect;
        if (external_call) {
            external_effect = effects_.resolve(
                *callee, context_.getSourceManager()
            );
            effect = external_effect.effective;
        }
        const bool fallible_r = effect.fallible_r || role == Role::r ||
            role == Role::entrypoint;
        const bool cpp_throw = effect.cpp_throw ||
            (role == Role::cxx && !is_nothrow(*callee));
        const bool returns_owner = effect.returns_owner ||
            returns_owner_value(*callee) || constructed_owner;
        calls.push_back({
            expression, callee, role, fallible_r, cpp_throw,
            returns_owner, effect.raw_acquire, effect.raw_release,
            construction, constructed_owner, external_call,
            external_effect
        });
    }

    bool VisitVarDecl(clang::VarDecl* variable)
    {
        if (!variable->isLocalVarDecl() ||
                llvm::isa<clang::ParmVarDecl>(variable))
            return true;

        locals.push_back(variable);

        if (variable->needsDestruction(context_) !=
                clang::QualType::DK_none ||
                is_owner_type(variable->getType())) {
            owners.push_back(variable);
        }
        return true;
    }

    bool VisitMaterializeTemporaryExpr(
        clang::MaterializeTemporaryExpr* expression
    ) {
        if (expression->getStorageDuration() != clang::SD_FullExpression &&
                is_cleanup_bearing_value(expression->getType())) {
            lifetime_owners.push_back(expression);
        }
        return true;
    }

    bool VisitCXXThrowExpr(clang::CXXThrowExpr* expression)
    {
        throws.push_back(expression);
        return true;
    }

    bool VisitCXXNewExpr(clang::CXXNewExpr* expression)
    {
        allocations.push_back(expression);
        return true;
    }

    bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expression)
    {
        deallocations.push_back(expression);
        return true;
    }

    bool VisitReturnStmt(clang::ReturnStmt* statement)
    {
        returns.push_back(statement);
        return true;
    }

    bool VisitIfStmt(clang::IfStmt* statement)
    {
        ifs.push_back(statement);
        return true;
    }

    bool VisitCallExpr(clang::CallExpr* expression)
    {
        record_call(
            expression, resolve_direct_callee(*expression), false, false
        );
        return true;
    }

    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expression)
    {
        const clang::QualType type = expression->getType();
        const bool constructed_owner =
            type.isDestructedType() != clang::QualType::DK_none ||
            is_owner_type(type);
        record_call(
            expression, expression->getConstructor(), true,
            constructed_owner
        );
        return true;
    }
};

const clang::LambdaExpr* call_lambda(const clang::CallExpr& call)
{
    for (const clang::Expr* argument : call.arguments()) {
        const clang::Expr* expression = argument->IgnoreParenImpCasts();
        if (const auto* lambda = llvm::dyn_cast<clang::LambdaExpr>(expression))
            return lambda;
    }
    return nullptr;
}

bool location_inside(
    const clang::SourceManager& source_manager,
    clang::SourceLocation location,
    clang::SourceRange range
) {
    location = source_manager.getExpansionLoc(location);
    clang::SourceLocation begin =
        source_manager.getExpansionLoc(range.getBegin());
    clang::SourceLocation end =
        source_manager.getExpansionLoc(range.getEnd());
    if (!location.isValid() || !begin.isValid() || !end.isValid())
        return false;
    if (source_manager.getFileID(location) != source_manager.getFileID(begin) ||
            source_manager.getFileID(location) != source_manager.getFileID(end))
        return false;
    return !source_manager.isBeforeInTranslationUnit(location, begin) &&
        !source_manager.isBeforeInTranslationUnit(end, location);
}

const clang::CXXTryStmt* enclosing_try(
    clang::ASTContext& context,
    const clang::Stmt& statement
) {
    clang::DynTypedNode current = clang::DynTypedNode::create(statement);
    for (;;) {
        const auto parents = context.getParents(current);
        if (parents.empty())
            return nullptr;
        const clang::DynTypedNode& parent = parents[0];
        if (const auto* try_statement = parent.get<clang::CXXTryStmt>())
            return try_statement;
        current = parent;
    }
}

bool is_descendant_of(
    clang::ASTContext& context,
    const clang::Stmt& child,
    const clang::Stmt& ancestor
) {
    llvm::SmallVector<clang::DynTypedNode, 16> pending;
    pending.push_back(clang::DynTypedNode::create(child));

    while (!pending.empty()) {
        const clang::DynTypedNode current = pending.pop_back_val();
        for (const clang::DynTypedNode& parent :
                context.getParents(current)) {
            const clang::Stmt* parent_statement = parent.get<clang::Stmt>();
            if (parent_statement == &ancestor)
                return true;
            pending.push_back(parent);
        }
    }

    return false;
}

bool is_named_record(clang::QualType type, llvm::StringRef name)
{
    type = type.getNonReferenceType();
    const clang::RecordType* record = type->getAs<clang::RecordType>();
    return record != nullptr &&
        record->getDecl()->getQualifiedNameAsString() == name;
}

bool is_vector_of_named_record(
    clang::QualType type,
    llvm::StringRef element_name
) {
    type = type.getNonReferenceType();
    const clang::RecordType* record = type->getAs<clang::RecordType>();
    const auto* specialization = record == nullptr
        ? nullptr
        : llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(
            record->getDecl()
        );
    if (specialization == nullptr ||
            specialization->getQualifiedNameAsString() != "std::vector") {
        return false;
    }

    const clang::TemplateArgumentList& arguments =
        specialization->getTemplateArgs();
    return arguments.size() > 0 &&
        arguments[0].getKind() == clang::TemplateArgument::Type &&
        is_named_record(arguments[0].getAsType(), element_name);
}

bool is_sexp_type(clang::QualType type)
{
    type = type.getCanonicalType();
    const clang::PointerType* pointer = type->getAs<clang::PointerType>();
    if (pointer == nullptr)
        return false;

    const clang::RecordType* record =
        pointer->getPointeeType()->getAs<clang::RecordType>();
    return record != nullptr &&
        record->getDecl()->getNameAsString() == "SEXPREC";
}

const clang::VarDecl* direct_variable(const clang::Expr* expression)
{
    if (expression == nullptr)
        return nullptr;
    expression = expression->IgnoreParenImpCasts();
    const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(expression);
    return reference == nullptr
        ? nullptr
        : llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
}

const clang::VarDecl* addressed_variable(const clang::Expr* expression)
{
    if (expression == nullptr)
        return nullptr;
    expression = expression->IgnoreParenImpCasts();
    const auto* address = llvm::dyn_cast<clang::UnaryOperator>(expression);
    if (address == nullptr || address->getOpcode() != clang::UO_AddrOf)
        return nullptr;
    return direct_variable(address->getSubExpr());
}

bool call_named(const CallRecord& call, llvm::StringRef name)
{
    return call.callee != nullptr &&
        call.callee->getQualifiedNameAsString() == name;
}

bool method_named(
    const CallRecord& call,
    llvm::StringRef parent,
    llvm::StringRef name
) {
    const auto* method = call.callee == nullptr
        ? nullptr
        : llvm::dyn_cast<clang::CXXMethodDecl>(call.callee);
    return method != nullptr &&
        method->getParent()->getQualifiedNameAsString() == parent &&
        method->getNameAsString() == name;
}

const clang::VarDecl* direct_object_variable(
    const clang::CXXMemberCallExpr& call
);

const clang::CFGBlock* find_cfg_block(
    const clang::CFG& cfg,
    clang::ASTContext& context,
    const clang::Stmt& target
);

const clang::VarDecl* call_object_variable(const CallRecord& call)
{
    const auto* member_call =
        llvm::dyn_cast<clang::CXXMemberCallExpr>(call.expression);
    return member_call == nullptr
        ? nullptr
        : direct_object_variable(*member_call);
}

bool source_before(
    const clang::SourceManager& source_manager,
    clang::SourceLocation before,
    clang::SourceLocation after
) {
    before = source_manager.getExpansionLoc(before);
    after = source_manager.getExpansionLoc(after);
    return before.isValid() && after.isValid() &&
        source_manager.getFileID(before) == source_manager.getFileID(after) &&
        source_manager.isBeforeInTranslationUnit(before, after);
}

const clang::VarDecl* enclosing_assignment_variable(
    clang::ASTContext& context,
    const clang::Stmt& statement
) {
    llvm::SmallVector<const clang::Stmt*, 16> pending;
    llvm::SmallPtrSet<const clang::Stmt*, 16> visited;
    pending.push_back(&statement);

    while (!pending.empty()) {
        const clang::Stmt* current = pending.pop_back_val();
        if (!visited.insert(current).second)
            continue;

        for (const clang::DynTypedNode& parent :
                context.getParents(*current)) {
            const clang::Stmt* parent_statement = parent.get<clang::Stmt>();
            if (parent_statement == nullptr)
                continue;
            if (const auto* assignment =
                    llvm::dyn_cast<clang::BinaryOperator>(parent_statement)) {
                if (assignment->isAssignmentOp())
                    return direct_variable(assignment->getLHS());
            }
            pending.push_back(parent_statement);
        }
    }

    return nullptr;
}

bool same_or_descendant_of(
    clang::ASTContext& context,
    const clang::Stmt& child,
    const clang::Stmt& ancestor
) {
    return &child == &ancestor || is_descendant_of(context, child, ancestor);
}

bool statement_dominates(
    const clang::CFG& cfg,
    clang::CFGDomTree& dominators,
    clang::ASTContext& context,
    const clang::SourceManager& source_manager,
    const clang::Stmt& before,
    const clang::Stmt& after
) {
    const clang::CFGBlock* before_block =
        find_cfg_block(cfg, context, before);
    const clang::CFGBlock* after_block =
        find_cfg_block(cfg, context, after);
    if (before_block == nullptr || after_block == nullptr ||
            !dominators.dominates(before_block, after_block)) {
        return false;
    }
    if (before_block != after_block)
        return true;

    bool saw_before = false;
    for (const clang::CFGElement& element : *before_block) {
        const auto statement = element.getAs<clang::CFGStmt>();
        if (!statement)
            continue;
        const clang::Stmt* current = statement->getStmt();
        if (current == &before ||
                same_or_descendant_of(context, before, *current)) {
            saw_before = true;
        }
        if (current == &after ||
                same_or_descendant_of(context, after, *current)) {
            return saw_before;
        }
    }
    return source_before(
        source_manager, before.getBeginLoc(), after.getBeginLoc()
    );
}

const clang::VarDecl* direct_object_variable(
    const clang::CXXMemberCallExpr& call
) {
    const clang::Expr* object = call.getImplicitObjectArgument();
    if (object == nullptr)
        return nullptr;
    object = object->IgnoreParenImpCasts();
    const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(object);
    return reference == nullptr
        ? nullptr
        : llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
}

const clang::VarDecl* indexed_object_variable(
    const clang::CXXMemberCallExpr& call
) {
    const clang::Expr* object = call.getImplicitObjectArgument();
    if (object == nullptr)
        return nullptr;
    object = object->IgnoreParenImpCasts();

    const auto* subscript =
        llvm::dyn_cast<clang::CXXOperatorCallExpr>(object);
    if (subscript == nullptr ||
            subscript->getOperator() != clang::OO_Subscript ||
            subscript->getNumArgs() < 1) {
        return nullptr;
    }
    return direct_variable(subscript->getArg(0));
}

bool is_default_construction(const clang::VarDecl& variable)
{
    const clang::Expr* initializer = variable.getInit();
    if (initializer == nullptr)
        return false;
    initializer = initializer->IgnoreParenImpCasts();
    const auto* construction =
        llvm::dyn_cast<clang::CXXConstructExpr>(initializer);
    return construction != nullptr && construction->getNumArgs() == 0;
}

const clang::CFGBlock* find_cfg_block(
    const clang::CFG& cfg,
    clang::ASTContext& context,
    const clang::Stmt& target
) {
    for (const clang::CFGBlock* block : cfg) {
        for (const clang::CFGElement& element : *block) {
            const auto statement = element.getAs<clang::CFGStmt>();
            if (statement && statement->getStmt() == &target)
                return block;
        }
    }

    for (const clang::CFGBlock* block : cfg) {
        for (const clang::CFGElement& element : *block) {
            const auto statement = element.getAs<clang::CFGStmt>();
            if (statement && is_descendant_of(
                    context, target, *statement->getStmt())) {
                return block;
            }
        }
    }

    return nullptr;
}

class FunctionChecker {
private:
    clang::ASTContext& context_;
    Reporter& reporter_;
    const ExternalEffects& effects_;

    void report(
        clang::SourceLocation location,
        const llvm::Twine& message
    ) {
        reporter_.error(
            context_.getSourceManager(), location, message.str()
        );
    }

    void check_external_manifest(const CallRecord& call)
    {
        if (!call.external_call || call.callee == nullptr)
            return;

        reporter_.external_call(
            *call.callee, call.external_effect,
            context_.getSourceManager(),
            call.expression->getExprLoc()
        );

        if (!effects_path.empty() &&
                !call.external_effect.manifest_present) {
            report(
                call.expression->getExprLoc(),
                llvm::Twine("calls unreviewed external function '") +
                    call.callee->getQualifiedNameAsString() + "'"
            );
        }
        else if (!effects_path.empty() &&
                !call.external_effect.manifest_matches) {
            const std::string message =
                (llvm::Twine("external effect manifest is stale for '") +
                    call.callee->getQualifiedNameAsString() + "': " +
                    call.external_effect.manifest_problem).str();
            if (call.external_effect.integrity_problem) {
                reporter_.integrity_error(
                    context_.getSourceManager(),
                    call.expression->getExprLoc(), message
                );
            }
            else {
                report(call.expression->getExprLoc(), message);
            }
        }
    }

    void check_readers(
        const BodyVisitor& body,
        const clang::LambdaExpr& lambda
    ) {
        std::vector<const clang::VarDecl*> readers;
        for (const clang::VarDecl* variable : body.locals) {
            if (is_named_record(variable->getType(), "charport::Reader") ||
                    is_vector_of_named_record(
                        variable->getType(), "charport::Reader"
                    )) {
                readers.push_back(variable);
            }
        }
        if (readers.empty())
            return;

        clang::CFG::BuildOptions options;
        options.setAllAlwaysAdd();
        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            lambda.getCallOperator(),
            const_cast<clang::Stmt*>(lambda.getBody()),
            &context_, options
        );
        if (!cfg) {
            report(
                lambda.getBeginLoc(),
                "cannot build a control-flow graph for Reader validation"
            );
            return;
        }

        clang::CFGDomTree dominators;
        dominators.buildDominatorTree(cfg.get());
        const clang::SourceManager& source_manager =
            context_.getSourceManager();
        const clang::SourceRange lambda_range = lambda.getSourceRange();

        for (const clang::VarDecl* reader : readers) {
            if (!is_default_construction(*reader)) {
                report(
                    reader->getLocation(),
                    llvm::Twine("Reader '") + reader->getName() +
                        "' must be default constructed in the owner region"
                );
            }

            std::vector<const clang::CXXMemberCallExpr*> resets;
            std::vector<const clang::CXXMemberCallExpr*> accesses;
            for (const CallRecord& call : body.calls) {
                const auto* member_call =
                    llvm::dyn_cast<clang::CXXMemberCallExpr>(
                        call.expression
                    );
                const auto* method = call.callee == nullptr
                    ? nullptr
                    : llvm::dyn_cast<clang::CXXMethodDecl>(call.callee);
                if (member_call == nullptr || method == nullptr ||
                        method->getParent()->getQualifiedNameAsString() !=
                            "charport::Reader") {
                    continue;
                }

                const clang::VarDecl* object =
                    direct_object_variable(*member_call);
                if (object == nullptr)
                    object = indexed_object_variable(*member_call);
                if (object == nullptr) {
                    if (reader == readers.front()) {
                        report(
                            member_call->getExprLoc(),
                            "Reader methods must use a direct owner-region variable or its indexed Reader vector"
                        );
                    }
                    continue;
                }
                if (object != reader)
                    continue;

                if (!location_inside(
                        source_manager, member_call->getExprLoc(),
                        lambda_range)) {
                    report(
                        member_call->getExprLoc(),
                        llvm::Twine("Reader '") + reader->getName() +
                            "' is used outside the unwind region"
                    );
                    continue;
                }

                if (method->getName() == "reset")
                    resets.push_back(member_call);
                else
                    accesses.push_back(member_call);
            }

            if (resets.size() != 1) {
                report(
                    reader->getLocation(),
                    llvm::Twine("Reader '") + reader->getName() +
                        "' must have exactly one reset in the unwind region"
                );
                continue;
            }

            const clang::CXXMemberCallExpr* reset = resets.front();
            const clang::CFGBlock* reset_block = find_cfg_block(
                *cfg, context_, *reset
            );
            if (reset_block == nullptr) {
                report(
                    reset->getExprLoc(),
                    llvm::Twine("cannot place Reader '") +
                        reader->getName() + "' reset in the control-flow graph"
                );
                continue;
            }

            for (const clang::CXXMemberCallExpr* access : accesses) {
                const clang::CFGBlock* access_block = find_cfg_block(
                    *cfg, context_, *access
                );
                bool dominated = access_block != nullptr &&
                    dominators.dominates(reset_block, access_block);
                if (dominated && reset_block == access_block) {
                    const clang::SourceLocation reset_location =
                        source_manager.getExpansionLoc(reset->getExprLoc());
                    const clang::SourceLocation access_location =
                        source_manager.getExpansionLoc(access->getExprLoc());
                    dominated = reset_location.isValid() &&
                        access_location.isValid() &&
                        source_manager.isBeforeInTranslationUnit(
                            reset_location, access_location
                        );
                }
                if (dominated)
                    continue;

                report(
                    access->getExprLoc(),
                    llvm::Twine("Reader '") + reader->getName() +
                        "' access is not dominated by its reset"
                );
            }
        }
    }

    void check_protection_shape(
        const clang::FunctionDecl& function,
        const BodyVisitor& body,
        const clang::CallExpr& unwind_call,
        const clang::LambdaExpr& lambda,
        const clang::CXXTryStmt& frame_try
    ) {
        if (!is_sexp_type(function.getReturnType()))
            return;

        const clang::FunctionDecl* unwind_callee =
            unwind_call.getDirectCallee();
        if (unwind_callee == nullptr ||
                unwind_callee->getQualifiedNameAsString() !=
                    "charr::shared::unwind_protect") {
            report(
                unwind_call.getExprLoc(),
                "SEXP entry point must use charr::shared::unwind_protect as its primary boundary"
            );
            return;
        }

        const clang::SourceManager& source_manager =
            context_.getSourceManager();
        const clang::SourceRange lambda_range = lambda.getSourceRange();
        const clang::SourceLocation frame_begin = frame_try.getBeginLoc();

        clang::CFG::BuildOptions function_options;
        function_options.setAllAlwaysAdd();
        std::unique_ptr<clang::CFG> function_cfg = clang::CFG::buildCFG(
            &function, const_cast<clang::Stmt*>(function.getBody()),
            &context_, function_options
        );
        clang::CFGDomTree function_dominators;
        if (!function_cfg) {
            report(
                function.getLocation(),
                "cannot build a control-flow graph for entry-point protection validation"
            );
        }
        else {
            function_dominators.buildDominatorTree(function_cfg.get());
        }

        std::vector<const clang::VarDecl*> protection_helpers;
        std::vector<const clang::VarDecl*> error_states;
        for (const clang::VarDecl* variable : body.locals) {
            if (is_named_record(
                    variable->getType(), "charr::shared::ProtHelper")) {
                protection_helpers.push_back(variable);
            }
            if (is_named_record(
                    variable->getType(),
                    "charr::shared::EntryErrorState")) {
                error_states.push_back(variable);
            }
        }

        const clang::VarDecl* entry_protections = nullptr;
        const clang::VarDecl* callback_protections = nullptr;
        if (protection_helpers.size() != 2) {
            report(
                function.getLocation(),
                llvm::Twine("SEXP entry point '") +
                    function.getQualifiedNameAsString() +
                    "' must contain exactly two ProtHelper locals"
            );
        }
        for (const clang::VarDecl* helper : protection_helpers) {
            if (helper->getName() == "entry_protections")
                entry_protections = helper;
            else if (helper->getName() == "callback_protections")
                callback_protections = helper;
            else
                report(
                    helper->getLocation(),
                    "ProtHelper must have the semantic role name 'entry_protections' or 'callback_protections'"
                );

            const clang::CXXRecordDecl* record =
                helper->getType()->getAsCXXRecordDecl();
            if (record == nullptr || record->getDefinition() == nullptr ||
                    !record->getDefinition()->hasTrivialDestructor()) {
                report(
                    helper->getLocation(),
                    "ProtHelper must remain trivially destructible"
                );
            }
            if (!source_before(
                    source_manager, helper->getLocation(),
                    frame_begin)) {
                report(
                    helper->getLocation(),
                    "ProtHelper must be declared before the owner try block"
                );
            }
        }
        if (entry_protections == nullptr)
            report(function.getLocation(), "SEXP entry point is missing entry_protections");
        if (callback_protections == nullptr)
            report(function.getLocation(), "SEXP entry point is missing callback_protections");

        const clang::VarDecl* error_state = nullptr;
        if (error_states.size() != 1) {
            report(
                function.getLocation(),
                llvm::Twine("SEXP entry point '") +
                    function.getQualifiedNameAsString() +
                    "' must contain exactly one EntryErrorState"
            );
        }
        else {
            error_state = error_states.front();
            if (!source_before(
                    source_manager, error_state->getLocation(), frame_begin)) {
                report(
                    error_state->getLocation(),
                    "EntryErrorState must be declared before the owner try block"
                );
            }
        }

        const clang::VarDecl* unwind_token = nullptr;
        if (unwind_call.getNumArgs() == 0 ||
                (unwind_token = direct_variable(
                    unwind_call.getArg(0))) == nullptr) {
            report(
                unwind_call.getExprLoc(),
                "trusted unwind call must receive a direct continuation-token variable"
            );
        }
        else {
            if (!source_before(
                    source_manager, unwind_token->getLocation(), frame_begin)) {
                report(
                    unwind_token->getLocation(),
                    "continuation token must be declared before the owner try block"
                );
            }

            const clang::Expr* initializer = unwind_token->getInit();
            initializer = initializer == nullptr
                ? nullptr
                : initializer->IgnoreParenImpCasts();
            const auto* protect_call =
                llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(initializer);
            const clang::Expr* protected_value =
                protect_call != nullptr && protect_call->getNumArgs() == 1
                    ? protect_call->getArg(0)->IgnoreParenImpCasts()
                    : nullptr;
            const auto* make_call =
                llvm::dyn_cast_or_null<clang::CallExpr>(protected_value);
            const clang::FunctionDecl* make_callee = make_call == nullptr
                ? nullptr
                : make_call->getDirectCallee();
            if (protect_call == nullptr ||
                    direct_object_variable(*protect_call) != entry_protections ||
                    protect_call->getMethodDecl() == nullptr ||
                    protect_call->getMethodDecl()->getName() != "protect_one" ||
                    make_callee == nullptr ||
                    make_callee->getQualifiedNameAsString() !=
                        "R_MakeUnwindCont") {
                report(
                    unwind_token->getLocation(),
                    "continuation token must be protected by entry_protections.protect_one(R_MakeUnwindCont())"
                );
            }
        }

        const clang::VarDecl* result = enclosing_assignment_variable(
            context_, unwind_call
        );
        if (result == nullptr || !is_sexp_type(result->getType())) {
            report(
                unwind_call.getExprLoc(),
                "trusted unwind result must be assigned to a direct SEXP result variable"
            );
            result = nullptr;
        }
        else if (!source_before(
                source_manager, result->getLocation(), frame_begin)) {
            report(
                result->getLocation(),
                "stable result variable must be declared before the owner try block"
            );
        }

        const clang::CXXMemberCallExpr* result_protection = nullptr;
        const clang::VarDecl* result_index = nullptr;
        unsigned result_protection_count = 0;
        if (result != nullptr) {
            for (const CallRecord& call : body.calls) {
                if (!method_named(
                        call, "charr::shared::ProtHelper",
                        "protect_with_index") ||
                        call_object_variable(call) != entry_protections)
                    continue;
                const auto* expression =
                    llvm::dyn_cast<clang::CXXMemberCallExpr>(call.expression);
                if (expression == nullptr || expression->getNumArgs() != 2 ||
                        direct_variable(expression->getArg(0)) != result) {
                    continue;
                }
                ++result_protection_count;
                result_protection = expression;
                result_index = addressed_variable(expression->getArg(1));
            }
        }

        if (result != nullptr && result_protection_count != 1) {
            report(
                result->getLocation(),
                llvm::Twine("stable result variable '") + result->getName() +
                    "' must have exactly one entry_protections.protect_with_index() before the owner region"
            );
        }
        else if (result_protection != nullptr) {
            if (result_index == nullptr) {
                report(
                    result_protection->getExprLoc(),
                    "entry result protection must use a direct PROTECT_INDEX variable"
                );
            }
            else if (!source_before(
                    source_manager, result_index->getLocation(), frame_begin)) {
                report(
                    result_index->getLocation(),
                    "stable result protection index must be declared before the owner try block"
                );
            }
            if (!source_before(
                    source_manager, result_protection->getExprLoc(),
                    frame_begin)) {
                report(
                    result_protection->getExprLoc(),
                    "stable result slot must be protected before the owner try block"
                );
            }
        }

        const auto is_raw_protection_call = [](const CallRecord& call) {
            if (call.callee == nullptr)
                return false;
            const std::string name =
                call.callee->getQualifiedNameAsString();
            return name == "Rf_protect" ||
                name == "R_ProtectWithIndex" ||
                name == "R_Reprotect" ||
                name == "Rf_unprotect" ||
                name == "Rf_unprotect_ptr" ||
                name == "R_PreserveObject" ||
                name == "R_ReleaseObject";
        };

        const auto is_raw_stack_release = [](const CallRecord& call) {
            return call_named(call, "Rf_unprotect") ||
                call_named(call, "Rf_unprotect_ptr");
        };

        for (const CallRecord& call : body.calls) {
            if (is_raw_protection_call(call)) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine("entry point uses raw R protection operation '") +
                        call.callee->getQualifiedNameAsString() +
                        "'; use the appropriate ProtHelper domain"
                );
            }
        }

        std::vector<const CallRecord*> callback_releases;
        std::vector<const CallRecord*> callback_reprotects;
        std::vector<const CallRecord*> callback_slot_protections;
        std::vector<const CallRecord*> callback_slot_reprotects;
        std::vector<const CallRecord*> callback_helper_operations;
        for (const CallRecord& call : body.calls) {
            const bool inside_lambda = location_inside(
                source_manager, call.expression->getExprLoc(), lambda_range
            );
            if (!inside_lambda)
                continue;
            const bool helper_method = method_named(
                call, "charr::shared::ProtHelper", "protect_one"
            ) || method_named(
                call, "charr::shared::ProtHelper", "protect_with_index"
            ) || method_named(
                call, "charr::shared::ProtHelper", "reprotect_one"
            ) || method_named(
                call, "charr::shared::ProtHelper", "reprotect_slot"
            ) || method_named(
                call, "charr::shared::ProtHelper", "release_all"
            ) || method_named(
                call, "charr::shared::ProtHelper", "adopt"
            ) || method_named(
                call, "charr::shared::ProtHelper", "release"
            ) || method_named(
                call, "charr::shared::ProtHelper", "clear"
            );
            const clang::VarDecl* helper = call_object_variable(call);
            const bool stable_result_reprotect = method_named(
                call, "charr::shared::ProtHelper", "reprotect_one"
            ) && helper == entry_protections;
            if (helper_method && helper != callback_protections &&
                    !stable_result_reprotect) {
                report(
                    call.expression->getExprLoc(),
                    "unwind-callback protections, slots, and cleanup must use callback_protections"
                );
            }
            if (method_named(
                    call, "charr::shared::ProtHelper", "release_all") &&
                    helper == callback_protections) {
                callback_releases.push_back(&call);
            }
            if (method_named(
                    call, "charr::shared::ProtHelper", "reprotect_one") &&
                    helper == entry_protections) {
                callback_reprotects.push_back(&call);
            }
            if (method_named(
                    call, "charr::shared::ProtHelper",
                    "protect_with_index") &&
                    helper == callback_protections) {
                callback_slot_protections.push_back(&call);
            }
            if (method_named(
                    call, "charr::shared::ProtHelper", "reprotect_slot") &&
                    helper == callback_protections) {
                callback_slot_reprotects.push_back(&call);
            }
            if (helper_method &&
                    !method_named(
                        call, "charr::shared::ProtHelper", "release_all") &&
                    !method_named(
                        call, "charr::shared::ProtHelper", "clear") &&
                    helper == callback_protections) {
                callback_helper_operations.push_back(&call);
            }
        }

        for (const CallRecord& call : body.calls) {
            const bool protect_method = method_named(
                call, "charr::shared::ProtHelper", "protect_one"
            ) || method_named(
                call, "charr::shared::ProtHelper", "protect_with_index"
            );
            if (protect_method && !location_inside(
                    source_manager, call.expression->getExprLoc(),
                    lambda_range)) {
                if (!source_before(
                        source_manager, call.expression->getExprLoc(),
                        frame_begin)) {
                    report(
                        call.expression->getExprLoc(),
                        "protection pushes outside the unwind callback must be confined to the trivial prelude"
                    );
                }
                else if (call_object_variable(call) != entry_protections) {
                    report(
                        call.expression->getExprLoc(),
                        "trivial-prelude protections must use entry_protections"
                    );
                }
            }
            if (method_named(
                    call, "charr::shared::ProtHelper", "clear")) {
                report(
                    call.expression->getExprLoc(),
                    "ProtHelper::clear() is forbidden in an entry point; use release_all()"
                );
            }
        }

        if (callback_releases.size() != 1) {
            report(
                lambda.getBeginLoc(),
                "unwind callback must contain exactly one ProtHelper::release_all()"
            );
        }

        if (result != nullptr && result_index != nullptr &&
                entry_protections != nullptr) {
            if (callback_reprotects.empty()) {
                report(
                    lambda.getBeginLoc(),
                    "unwind callback must update the stable result slot through ProtHelper::reprotect_one()"
                );
            }
            for (const CallRecord* call : callback_reprotects) {
                const auto* expression =
                    llvm::dyn_cast<clang::CXXMemberCallExpr>(call->expression);
                const clang::VarDecl* assigned =
                    enclosing_assignment_variable(context_, *call->expression);
                if (expression == nullptr || expression->getNumArgs() != 2 ||
                        direct_variable(expression->getArg(1)) != result_index ||
                        assigned != result) {
                    report(
                        call->expression->getExprLoc(),
                        "ProtHelper::reprotect_one() must assign the stable result using its PROTECT_INDEX"
                    );
                }
            }
        }

        clang::CFG::BuildOptions lambda_options;
        lambda_options.setAllAlwaysAdd();
        std::unique_ptr<clang::CFG> lambda_cfg = clang::CFG::buildCFG(
            lambda.getCallOperator(),
            const_cast<clang::Stmt*>(lambda.getBody()),
            &context_, lambda_options
        );
        if (!lambda_cfg) {
            report(
                lambda.getBeginLoc(),
                "cannot build a control-flow graph for protection validation"
            );
        }
        else {
            clang::CFGDomTree dominators;
            dominators.buildDominatorTree(lambda_cfg.get());

            for (const CallRecord* call : callback_slot_reprotects) {
                const auto* expression =
                    llvm::dyn_cast<clang::CXXMemberCallExpr>(
                        call->expression
                    );
                const clang::VarDecl* assigned =
                    enclosing_assignment_variable(
                        context_, *call->expression
                    );
                const clang::VarDecl* index =
                    expression != nullptr && expression->getNumArgs() == 2
                        ? direct_variable(expression->getArg(1))
                        : nullptr;
                if (assigned == nullptr ||
                        !is_sexp_type(assigned->getType()) ||
                        index == nullptr) {
                    report(
                        call->expression->getExprLoc(),
                        "ProtHelper::reprotect_slot() must assign a direct SEXP variable using a direct PROTECT_INDEX"
                    );
                    continue;
                }

                std::vector<const CallRecord*> matching_index;
                for (const CallRecord* protection :
                        callback_slot_protections) {
                    const auto* protect_expression =
                        llvm::dyn_cast<clang::CXXMemberCallExpr>(
                            protection->expression
                        );
                    if (protect_expression == nullptr ||
                            protect_expression->getNumArgs() != 2 ||
                            addressed_variable(
                                protect_expression->getArg(1)) != index) {
                        continue;
                    }
                    matching_index.push_back(protection);
                }

                if (matching_index.size() != 1) {
                    report(
                        call->expression->getExprLoc(),
                        "ProtHelper::reprotect_slot() must use a PROTECT_INDEX initialized by exactly one callback protect_with_index()"
                    );
                    continue;
                }

                const CallRecord* protection = matching_index.front();
                const auto* protect_expression =
                    llvm::cast<clang::CXXMemberCallExpr>(
                        protection->expression
                    );
                if (direct_variable(protect_expression->getArg(0)) !=
                        assigned) {
                    report(
                        call->expression->getExprLoc(),
                        "ProtHelper::reprotect_slot() must assign the variable initialized for its PROTECT_INDEX"
                    );
                    continue;
                }

                if (!statement_dominates(
                        *lambda_cfg, dominators, context_, source_manager,
                        *protection->expression, *call->expression)) {
                    report(
                        call->expression->getExprLoc(),
                        "ProtHelper::reprotect_slot() is not dominated by its protect_with_index()"
                    );
                }
            }

            if (callback_releases.size() == 1) {
                for (const CallRecord* call : callback_helper_operations) {
                    if (statement_dominates(
                            *lambda_cfg, dominators, context_, source_manager,
                            *callback_releases.front()->expression,
                            *call->expression)) {
                        report(
                            call->expression->getExprLoc(),
                            "ProtHelper operation appears after the callback's release_all()"
                        );
                    }
                }
                for (const clang::ReturnStmt* statement : body.returns) {
                    if (!same_or_descendant_of(
                            context_, *statement, *lambda.getBody())) {
                        continue;
                    }
                    if (!statement_dominates(
                            *lambda_cfg, dominators, context_, source_manager,
                            *callback_releases.front()->expression,
                            *statement)) {
                        report(
                            statement->getReturnLoc(),
                            "normal unwind-callback return is not dominated by ProtHelper::release_all()"
                        );
                    }
                }
            }
        }

        const auto find_error_if = [&](llvm::StringRef method_name) {
            std::vector<const clang::IfStmt*> matches;
            if (error_state == nullptr)
                return matches;
            for (const clang::IfStmt* statement : body.ifs) {
                if (location_inside(
                        source_manager, statement->getIfLoc(), lambda_range)) {
                    continue;
                }
                for (const CallRecord& call : body.calls) {
                    if (!method_named(
                            call, "charr::shared::EntryErrorState",
                            method_name) ||
                            call_object_variable(call) != error_state ||
                            !same_or_descendant_of(
                                context_, *call.expression,
                                *statement->getCond())) {
                        continue;
                    }
                    matches.push_back(statement);
                    break;
                }
            }
            return matches;
        };

        const std::vector<const clang::IfStmt*> r_error_ifs =
            find_error_if("has_r_error");
        const std::vector<const clang::IfStmt*> cpp_error_ifs =
            find_error_if("has_cpp_error");
        if (r_error_ifs.size() != 1) {
            report(
                function.getLocation(),
                "SEXP entry point must contain exactly one R-error continuation branch"
            );
        }
        if (cpp_error_ifs.size() != 1) {
            report(
                function.getLocation(),
                "SEXP entry point must contain exactly one C++-error conversion branch"
            );
        }

        const clang::IfStmt* r_error_if = r_error_ifs.size() == 1
            ? r_error_ifs.front()
            : nullptr;
        const clang::IfStmt* cpp_error_if = cpp_error_ifs.size() == 1
            ? cpp_error_ifs.front()
            : nullptr;

        if (r_error_if != nullptr) {
            for (const CallRecord& call : body.calls) {
                if (!function_cfg ||
                        !statement_dominates(
                            *function_cfg, function_dominators, context_,
                            source_manager, frame_try, *call.expression) ||
                        !statement_dominates(
                            *function_cfg, function_dominators, context_,
                            source_manager, *call.expression, *r_error_if)) {
                    continue;
                }

                const bool helper_cleanup = method_named(
                    call, "charr::shared::ProtHelper", "release_all"
                ) || method_named(
                    call, "charr::shared::ProtHelper", "clear"
                );
                if (call.fallible_r || is_raw_stack_release(call) ||
                        (helper_cleanup &&
                         (call_object_variable(call) == entry_protections ||
                          call_object_variable(call) == callback_protections))) {
                    report(
                        call.expression->getExprLoc(),
                        "pending R error must be continued before postlude R calls or protection cleanup"
                    );
                }
            }

            const clang::Stmt* branch = r_error_if->getThen();
            unsigned continuation_count = 0;
            for (const CallRecord& call : body.calls) {
                if (!same_or_descendant_of(
                        context_, *call.expression, *branch)) {
                    continue;
                }
                if (call_named(
                        call, "charr::shared::continue_r_unwind")) {
                    ++continuation_count;
                    const auto* expression =
                        llvm::dyn_cast<clang::CallExpr>(call.expression);
                    if (expression == nullptr ||
                            expression->getNumArgs() != 1 ||
                            direct_variable(expression->getArg(0)) !=
                                unwind_token) {
                        report(
                            call.expression->getExprLoc(),
                            "R-error branch must continue the trusted unwind token"
                        );
                    }
                }
                else if (call.fallible_r) {
                    report(
                        call.expression->getExprLoc(),
                        "R-error continuation branch must not make another fallible R call"
                    );
                }
                const bool helper_release = method_named(
                    call, "charr::shared::ProtHelper", "release"
                ) || method_named(
                    call, "charr::shared::ProtHelper", "release_all"
                ) || method_named(
                    call, "charr::shared::ProtHelper", "clear"
                );
                if (is_raw_stack_release(call) ||
                        (helper_release &&
                         (call_object_variable(call) == entry_protections ||
                          call_object_variable(call) == callback_protections))) {
                    report(
                        call.expression->getExprLoc(),
                        "R-error continuation branch must not release either protection domain"
                    );
                }
            }
            if (continuation_count != 1) {
                report(
                    r_error_if->getIfLoc(),
                    "R-error branch must contain exactly one continue_r_unwind() call"
                );
            }
        }

        if (cpp_error_if != nullptr) {
            const clang::Stmt* branch = cpp_error_if->getThen();
            std::vector<const CallRecord*> callback_releases_in_branch;
            std::vector<const CallRecord*> entry_releases_in_branch;
            std::vector<const CallRecord*> unprotects;
            std::vector<const CallRecord*> r_errors;
            for (const CallRecord& call : body.calls) {
                if (!same_or_descendant_of(
                        context_, *call.expression, *branch)) {
                    continue;
                }
                if (method_named(
                        call, "charr::shared::ProtHelper",
                        "release_all") &&
                        call_object_variable(call) == callback_protections) {
                    callback_releases_in_branch.push_back(&call);
                }
                if (method_named(
                        call, "charr::shared::ProtHelper",
                        "release_all") &&
                        call_object_variable(call) == entry_protections) {
                    entry_releases_in_branch.push_back(&call);
                }
                if (is_raw_stack_release(call))
                    unprotects.push_back(&call);
                if (call_named(call, "Rf_error"))
                    r_errors.push_back(&call);
            }
            if (callback_releases_in_branch.size() != 1) {
                report(
                    cpp_error_if->getIfLoc(),
                    "C++-error branch must release callback_protections exactly once"
                );
            }
            if (entry_releases_in_branch.size() != 1) {
                report(
                    cpp_error_if->getIfLoc(),
                    "C++-error branch must release entry_protections exactly once"
                );
            }
            if (!unprotects.empty()) {
                report(
                    unprotects.front()->expression->getExprLoc(),
                    "C++-error branch must use entry_protections instead of raw UNPROTECT"
                );
            }
            if (r_errors.size() != 1) {
                report(
                    cpp_error_if->getIfLoc(),
                    "C++-error branch must contain exactly one outer R error"
                );
            }
            if (function_cfg && callback_releases_in_branch.size() == 1 &&
                    entry_releases_in_branch.size() == 1 &&
                    r_errors.size() == 1 &&
                    (!statement_dominates(
                        *function_cfg, function_dominators, context_,
                        source_manager,
                        *callback_releases_in_branch.front()->expression,
                        *entry_releases_in_branch.front()->expression) ||
                     !statement_dominates(
                        *function_cfg, function_dominators, context_,
                        source_manager,
                        *entry_releases_in_branch.front()->expression,
                        *r_errors.front()->expression))) {
                report(
                    cpp_error_if->getIfLoc(),
                    "C++-error branch must release callback then entry protections before raising the R error"
                );
            }

            if (function_cfg && callback_releases_in_branch.size() == 1 &&
                    entry_releases_in_branch.size() == 1) {
                for (const CallRecord& call : body.calls) {
                    if (!call.fallible_r || call_named(call, "Rf_error") ||
                            !same_or_descendant_of(
                                context_, *call.expression, *branch)) {
                        continue;
                    }
                    if (!statement_dominates(
                            *function_cfg, function_dominators, context_,
                            source_manager,
                            *callback_releases_in_branch.front()->expression,
                            *call.expression) ||
                            statement_dominates(
                                *function_cfg, function_dominators, context_,
                                source_manager,
                                *entry_releases_in_branch.front()->expression,
                                *call.expression)) {
                        report(
                            call.expression->getExprLoc(),
                            "C++-error postlude R calls must run after callback cleanup and before entry cleanup"
                        );
                    }
                }
            }
        }

        for (const CallRecord& call : body.calls) {
            if (!is_raw_stack_release(call))
                continue;
            if (source_before(
                    source_manager, call.expression->getExprLoc(),
                    frame_begin) ||
                    location_inside(
                        source_manager, call.expression->getExprLoc(),
                        frame_try.getSourceRange())) {
                report(
                    call.expression->getExprLoc(),
                    "prelude protections must remain until the owner region has ended"
                );
            }
        }

        std::vector<const clang::ReturnStmt*> outer_returns;
        for (const clang::ReturnStmt* statement : body.returns) {
            if (!same_or_descendant_of(
                    context_, *statement, *lambda.getBody())) {
                outer_returns.push_back(statement);
            }
        }
        if (outer_returns.size() != 1) {
            report(
                function.getLocation(),
                "SEXP entry point must have one final successful return"
            );
            return;
        }

        const clang::ReturnStmt* final_return = outer_returns.front();
        if (result != nullptr &&
                direct_variable(final_return->getRetValue()) != result) {
            report(
                final_return->getReturnLoc(),
                "final successful return must return the stable result variable"
            );
        }

        if (!function_cfg) {
            return;
        }

        std::vector<const CallRecord*> success_entry_releases;
        for (const CallRecord& call : body.calls) {
            if (!method_named(
                    call, "charr::shared::ProtHelper", "release_all") ||
                    call_object_variable(call) != entry_protections ||
                    same_or_descendant_of(
                        context_, *call.expression, *lambda.getBody()) ||
                    (r_error_if != nullptr && same_or_descendant_of(
                        context_, *call.expression, *r_error_if->getThen())) ||
                    (cpp_error_if != nullptr && same_or_descendant_of(
                        context_, *call.expression, *cpp_error_if->getThen()))) {
                continue;
            }
            if (statement_dominates(
                    *function_cfg, function_dominators, context_,
                    source_manager, *call.expression, *final_return)) {
                success_entry_releases.push_back(&call);
            }
        }
        if (success_entry_releases.size() != 1) {
            report(
                final_return->getReturnLoc(),
                "successful return must be dominated by one entry_protections.release_all()"
            );
        }
        else {
            for (const CallRecord& call : body.calls) {
                if (!call.fallible_r || call.expression == &unwind_call ||
                        same_or_descendant_of(
                            context_, *call.expression, *lambda.getBody()) ||
                        (r_error_if != nullptr && same_or_descendant_of(
                            context_, *call.expression,
                            *r_error_if->getThen())) ||
                        (cpp_error_if != nullptr && same_or_descendant_of(
                            context_, *call.expression,
                            *cpp_error_if->getThen()))) {
                    continue;
                }
                if (statement_dominates(
                        *function_cfg, function_dominators, context_,
                        source_manager,
                        *success_entry_releases.front()->expression,
                        *call.expression)) {
                    report(
                        call.expression->getExprLoc(),
                        "successful-path postlude R calls must precede entry protection cleanup"
                    );
                }
            }
        }
    }

    void check_entrypoint(
        const clang::FunctionDecl& function,
        const BodyVisitor& body
    ) {
        std::vector<const clang::CallExpr*> unwind_calls;
        for (const CallRecord& call : body.calls) {
            if (call.role != Role::trusted_unwind)
                continue;
            if (const auto* expression =
                    llvm::dyn_cast<clang::CallExpr>(call.expression)) {
                unwind_calls.push_back(expression);
            }
        }

        if (unwind_calls.size() != 1) {
            report(
                function.getLocation(),
                llvm::Twine("entry point '") +
                    function.getQualifiedNameAsString() +
                    "' must contain exactly one trusted unwind call"
            );
            return;
        }

        const clang::LambdaExpr* lambda = call_lambda(*unwind_calls.front());
        if (lambda == nullptr) {
            report(
                unwind_calls.front()->getExprLoc(),
                "trusted unwind call must receive the unwind lambda directly"
            );
            return;
        }

        const clang::CXXTryStmt* frame_try =
            enclosing_try(context_, *unwind_calls.front());
        if (frame_try == nullptr) {
            report(
                unwind_calls.front()->getExprLoc(),
                "trusted unwind call must be inside the entry point's owner try block"
            );
            return;
        }

        const clang::SourceRange lambda_range = lambda->getSourceRange();
        const clang::SourceRange frame_range =
            frame_try->getTryBlock()->getSourceRange();
        for (const clang::VarDecl* owner : body.owners) {
            if (location_inside(
                    context_.getSourceManager(), owner->getLocation(),
                    lambda_range)) {
                report(
                    owner->getLocation(),
                    llvm::Twine("cleanup-bearing local '") +
                        owner->getName() +
                        "' is owned by the unwind region"
                );
            }
            else if (!location_inside(
                    context_.getSourceManager(), owner->getLocation(),
                    frame_range)) {
                report(
                    owner->getLocation(),
                    llvm::Twine("cleanup-bearing local '") +
                        owner->getName() +
                        "' is outside the entry point's owner region"
                );
            }
            else if (!source_before(
                    context_.getSourceManager(), owner->getLocation(),
                    unwind_calls.front()->getExprLoc())) {
                report(
                    owner->getLocation(),
                    llvm::Twine("cleanup-bearing local '") +
                        owner->getName() +
                        "' must be declared before the primary unwind boundary"
                );
            }
        }
        for (const clang::MaterializeTemporaryExpr* owner :
                body.lifetime_owners) {
            if (location_inside(
                    context_.getSourceManager(), owner->getExprLoc(),
                    lambda_range)) {
                report(
                    owner->getExprLoc(),
                    "cleanup-bearing temporary has an unwind-region lifetime"
                );
            }
            else if (!location_inside(
                    context_.getSourceManager(), owner->getExprLoc(),
                    frame_range)) {
                report(
                    owner->getExprLoc(),
                    "cleanup-bearing temporary is outside the entry point's owner region"
                );
            }
            else if (!source_before(
                    context_.getSourceManager(), owner->getExprLoc(),
                    unwind_calls.front()->getExprLoc())) {
                report(
                    owner->getExprLoc(),
                    "cleanup-bearing temporary must precede the primary unwind boundary"
                );
            }
        }

        check_readers(body, *lambda);
        check_protection_shape(
            function, body, *unwind_calls.front(), *lambda, *frame_try
        );

        for (const clang::CXXNewExpr* allocation : body.allocations) {
            if (location_inside(
                    context_.getSourceManager(),
                    allocation->getExprLoc(), lambda_range)) {
                report(
                    allocation->getExprLoc(),
                    "unwind region contains a native allocation expression"
                );
            }
        }
        for (const clang::CXXDeleteExpr* deallocation : body.deallocations) {
            if (location_inside(
                    context_.getSourceManager(),
                    deallocation->getExprLoc(), lambda_range)) {
                report(
                    deallocation->getExprLoc(),
                    "unwind region contains a native deallocation expression"
                );
            }
        }

        for (const clang::CXXThrowExpr* expression : body.throws) {
            if (!location_inside(
                    context_.getSourceManager(),
                    expression->getThrowLoc(), frame_range)) {
                report(
                    expression->getThrowLoc(),
                    "entry point contains a C++ throw outside its owner try block"
                );
            }
        }

        for (const CallRecord& call : body.calls) {
            const bool inside_lambda = location_inside(
                context_.getSourceManager(),
                call.expression->getExprLoc(), lambda_range
            );

            if (inside_lambda && call.construction &&
                    call.constructed_owner && call.fallible_r) {
                report(
                    call.expression->getExprLoc(),
                    "fallible R construction creates an owner in the unwind region"
                );
            }

            if (inside_lambda && call.returns_owner) {
                for (const CallRecord& r_call : body.calls) {
                    if (!r_call.fallible_r ||
                            r_call.expression == call.expression ||
                            !location_inside(
                                context_.getSourceManager(),
                                r_call.expression->getExprLoc(),
                                lambda_range)) {
                        continue;
                    }
                    if (!is_descendant_of(
                            context_, *call.expression,
                            *r_call.expression)) {
                        continue;
                    }

                    report(
                        call.expression->getExprLoc(),
                        "ownership-returning expression is nested in a fallible R call"
                    );
                    break;
                }
            }

            if (call.cpp_throw && !location_inside(
                    context_.getSourceManager(),
                    call.expression->getExprLoc(), frame_range)) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine("potentially throwing C++ call '") +
                        (call.callee == nullptr
                            ? llvm::StringRef("<indirect>")
                            : llvm::StringRef(
                                call.callee->getQualifiedNameAsString())) +
                        "' appears outside the entry point's owner try block"
                );
            }

            if (!call.fallible_r || call.expression == unwind_calls.front())
                continue;
            if (inside_lambda) {
                continue;
            }
            if (!location_inside(
                    context_.getSourceManager(),
                    call.expression->getExprLoc(),
                    frame_try->getTryBlock()->getSourceRange())) {
                continue;
            }

            report(
                call.expression->getExprLoc(),
                llvm::Twine("fallible R call '") +
                    (call.callee == nullptr
                        ? llvm::StringRef("<indirect>")
                        : llvm::StringRef(
                            call.callee->getQualifiedNameAsString())) +
                    "' appears outside the unwind region"
            );
        }
    }

    void check_abi_shim(const clang::FunctionDecl& function)
    {
        if (!function.isExternC()) {
            report(function.getLocation(), "ABI shim must have extern \"C\" linkage");
        }
        if (!is_nothrow(function)) {
            report(function.getLocation(), "ABI shim must be noexcept");
        }
        if (!is_sexp_type(function.getReturnType())) {
            report(function.getLocation(), "ABI shim must return SEXP");
        }
        for (const clang::ParmVarDecl* parameter : function.parameters()) {
            if (!is_sexp_type(parameter->getType())) {
                report(parameter->getLocation(), "ABI shim parameters must all be SEXP");
            }
        }

        const auto* body = llvm::dyn_cast<clang::CompoundStmt>(function.getBody());
        if (body == nullptr || body->size() != 1) {
            report(
                function.getLocation(),
                "ABI shim body must contain one direct return call to a CHARR_ENTRYPOINT"
            );
            return;
        }
        const auto* return_statement =
            llvm::dyn_cast<clang::ReturnStmt>(*body->body_begin());
        const clang::Expr* value = return_statement == nullptr
            ? nullptr
            : return_statement->getRetValue();
        value = value == nullptr ? nullptr : value->IgnoreParenImpCasts();
        const auto* call = llvm::dyn_cast_or_null<clang::CallExpr>(value);
        const clang::FunctionDecl* callee = call == nullptr
            ? nullptr
            : resolve_direct_callee(*call);
        if (call == nullptr || callee == nullptr ||
                annotation_role(*callee) != Role::entrypoint) {
            report(
                function.getLocation(),
                "ABI shim must directly return a call to a CHARR_ENTRYPOINT"
            );
            return;
        }
        if (call->getNumArgs() != function.getNumParams()) {
            report(
                call->getExprLoc(),
                "ABI shim must forward all SEXP parameters directly and in order"
            );
            return;
        }
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            if (direct_variable(call->getArg(i)) != function.getParamDecl(i)) {
                report(
                    call->getArg(i)->getExprLoc(),
                    "ABI shim must forward all SEXP parameters directly and in order"
                );
            }
        }
    }

public:
    FunctionChecker(
        clang::ASTContext& context,
        Reporter& reporter,
        const ExternalEffects& effects
    ) : context_(context), reporter_(reporter), effects_(effects) {}

    void check(const clang::FunctionDecl& function)
    {
        const Role role = annotation_role(function);
        if (role == Role::conflicting) {
            report(
                function.getLocation(),
                llvm::Twine("function '") +
                    function.getQualifiedNameAsString() +
                    "' has conflicting lint roles"
            );
            return;
        }
        if (role == Role::none) {
            report(
                function.getLocation(),
                llvm::Twine("function '") +
                    function.getQualifiedNameAsString() +
                    "' has no lint role"
            );
            if (!audit_mode && !dump_external_calls &&
                    write_effects_manifest.empty())
                return;
        }
        BodyVisitor body(context_, effects_);
        if (const auto* constructor =
                llvm::dyn_cast<clang::CXXConstructorDecl>(&function)) {
            for (const clang::CXXCtorInitializer* initializer :
                    constructor->inits()) {
                body.TraverseStmt(
                    const_cast<clang::Expr*>(initializer->getInit())
                );
            }
        }
        body.TraverseStmt(const_cast<clang::Stmt*>(function.getBody()));

        if (role == Role::trusted_unwind) {
            for (const CallRecord& call : body.calls)
                check_external_manifest(call);
            return;
        }

        if ((role == Role::r || role == Role::neutral ||
                role == Role::entrypoint) && !is_nothrow(function)) {
            report(
                function.getLocation(),
                llvm::Twine(role_name(role)) + " '" +
                    function.getQualifiedNameAsString() +
                    "' must be noexcept"
            );
        }

        if (role == Role::r || role == Role::neutral) {
            if (returns_owner_value(function)) {
                report(
                    function.getLocation(),
                    llvm::Twine(role_name(role)) + " '" +
                        function.getQualifiedNameAsString() +
                        "' returns a cleanup-bearing value"
                );
            }
            for (const clang::ParmVarDecl* parameter :
                    function.parameters()) {
                if (!is_cleanup_bearing_value(parameter->getType()))
                    continue;
                report(
                    parameter->getLocation(),
                    llvm::Twine(role_name(role)) +
                        " owns cleanup-bearing parameter '" +
                        parameter->getName() + "'"
                );
            }
            for (const clang::VarDecl* owner : body.owners) {
                report(
                    owner->getLocation(),
                    llvm::Twine(role_name(role)) + " stores cleanup-bearing local '" +
                        owner->getName() + "'"
                );
            }
            for (const clang::MaterializeTemporaryExpr* owner :
                    body.lifetime_owners) {
                report(
                    owner->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " extends a cleanup-bearing temporary lifetime"
                );
            }
        }

        if (role == Role::r || role == Role::neutral) {
            for (const clang::CXXThrowExpr* expression : body.throws) {
                report(
                    expression->getThrowLoc(),
                    llvm::Twine(role_name(role)) +
                        " contains a C++ throw expression"
                );
            }
            for (const clang::CXXNewExpr* expression : body.allocations) {
                report(
                    expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " contains a native allocation expression"
                );
            }
            for (const clang::CXXDeleteExpr* expression :
                    body.deallocations) {
                report(
                    expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " contains a native deallocation expression"
                );
            }
        }

        for (const CallRecord& call : body.calls) {
            if (call.callee == nullptr) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " contains an indirect or unresolved call"
                );
                continue;
            }

            if (call.role == Role::none &&
                    is_charr_owned(
                        *call.callee, context_.getSourceManager())) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " calls unclassified charr function '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }
            check_external_manifest(call);

            if (role == Role::neutral && call.role == Role::cxx) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine("neutral helper calls C++ helper '") +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if (role != Role::entrypoint && role != Role::abi_shim &&
                    call.role == Role::entrypoint) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) + " calls entry point '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if ((role == Role::cxx || role == Role::neutral) &&
                    call.fallible_r) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) + " calls fallible R operation '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if (role == Role::r && call.cpp_throw) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine("R helper calls potentially throwing operation '") +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if (role == Role::neutral && call.cpp_throw) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine("neutral helper calls potentially throwing operation '") +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if ((role == Role::r || role == Role::neutral) &&
                    call.returns_owner) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " calls an ownership-returning operation '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if ((role == Role::r || role == Role::neutral ||
                    role == Role::entrypoint) && call.raw_acquire) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " directly calls raw resource acquisition '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if ((role == Role::r || role == Role::neutral ||
                    role == Role::entrypoint) && call.raw_release) {
                report(
                    call.expression->getExprLoc(),
                    llvm::Twine(role_name(role)) +
                        " directly calls raw resource release '" +
                        call.callee->getQualifiedNameAsString() + "'"
                );
            }

            if (role != Role::entrypoint &&
                    call.role == Role::trusted_unwind) {
                report(
                    call.expression->getExprLoc(),
                    "trusted unwind intrinsic may be called only by an entry point"
                );
            }
        }

        if (role == Role::entrypoint)
            check_entrypoint(function, body);
        else if (role == Role::abi_shim)
            check_abi_shim(function);
    }
};

struct SourcePoint {
    std::string path;
    unsigned line = 0;
    unsigned column = 0;
};

std::string normalize_path(llvm::StringRef input)
{
    if (input.empty())
        return "";

    llvm::SmallString<256> path(input);
    llvm::sys::path::remove_dots(path, true);

    llvm::SmallString<256> current;
    if (!llvm::sys::fs::current_path(current)) {
        llvm::sys::path::remove_dots(current, true);
        const llvm::StringRef normalized = path;
        const llvm::StringRef root = current;
        if (normalized.starts_with(root) && normalized.size() > root.size() &&
                llvm::sys::path::is_separator(normalized[root.size()])) {
            const std::string relative =
                normalized.drop_front(root.size() + 1).str();
            path = relative;
        }
    }

    std::string output = path.str().str();
    std::replace(output.begin(), output.end(), '\\', '/');
    if (llvm::StringRef(output).starts_with("base_backend/") ||
            llvm::StringRef(output).starts_with("altrep_backend/") ||
            llvm::StringRef(output).starts_with("shared/") ||
            llvm::StringRef(output).starts_with("runtime/")) {
        output = "src/" + output;
    }
    return output;
}

bool is_cpp_path(llvm::StringRef path)
{
    return path.ends_with(".cpp") || path.ends_with(".cc") ||
        path.ends_with(".cxx") || path.ends_with(".c");
}

bool is_project_path(llvm::StringRef path)
{
    return path.starts_with("src/") || path.starts_with("tools/");
}

bool is_code_map_owned_path(llvm::StringRef path)
{
    return path.starts_with("src/") &&
        !path.starts_with("src/icu78/");
}

std::string module_for_path(llvm::StringRef path)
{
    if (path.starts_with("src/base_backend/"))
        return "base";
    if (path.starts_with("src/altrep_backend/"))
        return "altrep";
    if (path.starts_with("src/shared/"))
        return "shared";
    if (path.starts_with("src/runtime/"))
        return "runtime";
    if (path.starts_with("src/icu"))
        return "icu";
    if (path.starts_with("tools/"))
        return "tools";
    return "external";
}

std::string expected_namespace_for_path(llvm::StringRef path)
{
    struct NamespaceRoot {
        llvm::StringLiteral directory;
        llvm::StringLiteral namespace_name;
    };
    static constexpr NamespaceRoot roots[] = {
        {"src/base_backend/", "charr::base_backend"},
        {"src/altrep_backend/", "charr::altrep_backend"},
        {"src/shared/", "charr::shared"},
        {"src/runtime/", "charr::runtime"}
    };

    for (const NamespaceRoot& root : roots) {
        if (!path.starts_with(root.directory))
            continue;

        llvm::StringRef relative = path.drop_front(root.directory.size());
        const std::size_t slash = relative.rfind('/');
        std::string output = root.namespace_name.str();
        if (slash == llvm::StringRef::npos)
            return output;

        llvm::StringRef directories = relative.take_front(slash);
        while (!directories.empty()) {
            const auto split = directories.split('/');
            if (!split.first.empty()) {
                output += "::";
                output += split.first.str();
            }
            directories = split.second;
        }
        return output;
    }
    return "";
}

std::string namespace_for_decl(const clang::Decl& declaration)
{
    std::vector<std::string> components;
    for (const clang::DeclContext* context = declaration.getDeclContext();
            context != nullptr; context = context->getParent()) {
        const auto* name_space = llvm::dyn_cast<clang::NamespaceDecl>(context);
        if (name_space == nullptr)
            continue;
        if (name_space->isAnonymousNamespace())
            components.push_back("(anonymous namespace)");
        else if (!name_space->getName().empty())
            components.push_back(name_space->getNameAsString());
    }

    std::reverse(components.begin(), components.end());
    std::string output;
    for (const std::string& component : components) {
        if (!output.empty())
            output += "::";
        output += component;
    }
    return output;
}

std::string linkage_name(clang::Linkage linkage)
{
    switch (linkage) {
    case clang::Linkage::Invalid: return "invalid";
    case clang::Linkage::None: return "none";
    case clang::Linkage::Internal: return "internal";
    case clang::Linkage::UniqueExternal: return "unique_external";
    case clang::Linkage::VisibleNone: return "visible_none";
    case clang::Linkage::Module: return "module";
    case clang::Linkage::External: return "external";
    }
    return "unknown";
}

const clang::FunctionDecl* function_family(
    const clang::FunctionDecl& function
) {
    const clang::FunctionDecl* current = &function;
    for (;;) {
        const clang::FunctionDecl* pattern =
            current->getTemplateInstantiationPattern(false);
        if (pattern == nullptr || pattern == current)
            break;
        current = pattern;
    }
    if (const clang::FunctionTemplateDecl* primary =
            current->getPrimaryTemplate()) {
        current = primary->getTemplatedDecl();
    }
    return current->getCanonicalDecl();
}

const clang::RecordDecl* record_family(const clang::RecordDecl& record)
{
    const clang::RecordDecl* current = &record;
    if (const auto* cxx = llvm::dyn_cast<clang::CXXRecordDecl>(current)) {
        if (const clang::CXXRecordDecl* pattern =
                cxx->getTemplateInstantiationPattern()) {
            current = pattern;
        }
    }
    return llvm::cast<clang::RecordDecl>(current->getCanonicalDecl());
}

SourcePoint source_point(
    const clang::Decl& declaration,
    const clang::SourceManager& source_manager,
    bool prefer_expansion = false
) {
    clang::SourceLocation location = prefer_expansion
        ? source_manager.getExpansionLoc(declaration.getLocation())
        : source_manager.getSpellingLoc(declaration.getLocation());
    llvm::StringRef filename = location.isValid()
        ? source_manager.getFilename(location)
        : llvm::StringRef();
    if (location.isInvalid() || filename.empty() ||
            filename == "<scratch space>") {
        location = source_manager.getExpansionLoc(declaration.getLocation());
    }

    SourcePoint output;
    if (location.isInvalid())
        return output;
    const clang::PresumedLoc presumed =
        source_manager.getPresumedLoc(location);
    if (!presumed.isValid())
        return output;
    const clang::FileEntry* file = source_manager.getFileEntryForID(
        source_manager.getFileID(location)
    );
    const llvm::StringRef physical = file == nullptr
        ? llvm::StringRef()
        : file->tryGetRealPathName();
    output.path = normalize_path(
        physical.empty() ? presumed.getFilename() : physical
    );
    output.line = presumed.getLine();
    output.column = presumed.getColumn();
    return output;
}

SourcePoint source_point(
    clang::SourceLocation location,
    const clang::SourceManager& source_manager
) {
    const clang::SourceLocation original = location;
    location = source_manager.getSpellingLoc(location);
    llvm::StringRef filename = location.isValid()
        ? source_manager.getFilename(location)
        : llvm::StringRef();
    if (location.isInvalid() || filename.empty() ||
            filename == "<scratch space>") {
        location = source_manager.getExpansionLoc(original);
    }

    SourcePoint output;
    if (location.isInvalid())
        return output;
    const clang::PresumedLoc presumed =
        source_manager.getPresumedLoc(location);
    if (!presumed.isValid())
        return output;
    const clang::FileEntry* file = source_manager.getFileEntryForID(
        source_manager.getFileID(location)
    );
    const llvm::StringRef physical = file == nullptr
        ? llvm::StringRef()
        : file->tryGetRealPathName();
    output.path = normalize_path(
        physical.empty() ? presumed.getFilename() : physical
    );
    output.line = presumed.getLine();
    output.column = presumed.getColumn();
    return output;
}

std::string declaration_id(
    const clang::Decl& declaration,
    llvm::StringRef fallback_kind,
    llvm::StringRef fallback_name,
    llvm::StringRef fallback_signature,
    const SourcePoint& fallback_location
) {
    llvm::SmallString<256> usr;
    if (!clang::index::generateUSRForDecl(&declaration, usr) && !usr.empty())
        return "decl:" + usr.str().str();

    std::string output = "fallback:" + fallback_kind.str() + ":";
    output += fallback_name.str();
    output += ":";
    output += fallback_signature.str();
    output += ":";
    output += fallback_location.path;
    output += ":";
    output += std::to_string(fallback_location.line);
    return output;
}

struct CodeMapEntity {
    std::string id;
    std::string kind;
    std::string name;
    std::string qualified_name;
    std::string namespace_name;
    std::string expected_namespace;
    std::string namespace_path_match;
    std::string defining_path;
    unsigned defining_line = 0;
    std::string home_compilation_unit;
    std::string module;
    std::string lint_tag;
    std::string signature;
    std::string nothrow;
    std::string parent_id;
    std::string owner_type;
    std::string trivial_destructor;
    std::string linkage;
    unsigned definition_rank = 0;
    std::set<std::string> seen_in_units;
};

struct CodeMapRelationship {
    std::string source_id;
    std::string target_id;
    std::string relationship;
    std::string source_path;
    unsigned source_line = 0;
    unsigned source_column = 0;
    unsigned count = 1;
};

struct UnitDependency {
    std::string source_unit;
    std::string target_unit;
    std::string source_path;
    std::string target_path;
    std::string relationship;
    unsigned edge_count = 0;
    std::set<std::string> source_entities;
};

std::string tsv_escape(llvm::StringRef value)
{
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '\t': output += "\\t"; break;
        case '\r': output += "\\r"; break;
        case '\n': output += "\\n"; break;
        default: output += character; break;
        }
    }
    return output;
}

std::string js_escape(llvm::StringRef value)
{
    std::string output;
    output.reserve(value.size() + 2);
    for (const unsigned char character : value.bytes()) {
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                static const char hex[] = "0123456789abcdef";
                output += "\\u00";
                output += hex[(character >> 4) & 0xf];
                output += hex[character & 0xf];
            }
            else {
                output += static_cast<char>(character);
            }
            break;
        }
    }
    return output;
}

class CodeMap {
private:
    std::map<std::string, CodeMapEntity> entities_;
    std::map<std::string, CodeMapRelationship> relationships_;
    std::set<std::string> unresolved_callsites_;
    bool finalized_ = false;

    static std::string relationship_key(
        llvm::StringRef source_id,
        llvm::StringRef target_id,
        llvm::StringRef relationship,
        const SourcePoint& location
    ) {
        std::string output = source_id.str();
        output += '\t';
        output += target_id.str();
        output += '\t';
        output += relationship.str();
        output += '\t';
        output += location.path;
        output += '\t';
        output += std::to_string(location.line);
        output += '\t';
        output += std::to_string(location.column);
        return output;
    }

    void merge_entity(CodeMapEntity candidate)
    {
        auto found = entities_.find(candidate.id);
        if (found == entities_.end()) {
            entities_.insert(std::make_pair(candidate.id, std::move(candidate)));
            return;
        }

        CodeMapEntity& current = found->second;
        current.seen_in_units.insert(
            candidate.seen_in_units.begin(), candidate.seen_in_units.end()
        );
        if (candidate.definition_rank <= current.definition_rank)
            return;

        std::set<std::string> seen = std::move(current.seen_in_units);
        current = std::move(candidate);
        current.seen_in_units.insert(seen.begin(), seen.end());
    }

    void ensure_file(llvm::StringRef path, llvm::StringRef current_unit)
    {
        if (path.empty() || !is_project_path(path))
            return;
        CodeMapEntity entity;
        entity.id = "file:" + path.str();
        entity.kind = "file";
        entity.name = llvm::sys::path::filename(path).str();
        entity.qualified_name = path.str();
        entity.defining_path = path.str();
        entity.module = module_for_path(path);
        entity.definition_rank = 2;
        if (!current_unit.empty())
            entity.seen_in_units.insert(current_unit.str());
        merge_entity(std::move(entity));
    }

    void finalize_relationships()
    {
        if (finalized_)
            return;
        finalized_ = true;

        std::vector<CodeMapEntity> snapshot;
        snapshot.reserve(entities_.size());
        for (const auto& item : entities_)
            snapshot.push_back(item.second);

        for (const CodeMapEntity& entity : snapshot) {
            if (entity.kind == "compilation_unit") {
                ensure_file(entity.defining_path, entity.id);
                add_relationship(
                    entity.id, "file:" + entity.defining_path,
                    "contains", {entity.defining_path, 0, 0}
                );
                continue;
            }
            if (entity.kind == "file")
                continue;
            if (!entity.defining_path.empty() &&
                    is_project_path(entity.defining_path)) {
                ensure_file(entity.defining_path, "");
                add_relationship(
                    "file:" + entity.defining_path, entity.id,
                    "contains", {entity.defining_path,
                        entity.defining_line, 0}
                );
            }
            if (!entity.parent_id.empty()) {
                add_relationship(
                    entity.parent_id, entity.id, "contains",
                    {entity.defining_path, entity.defining_line, 0}
                );
            }
        }
    }

    std::vector<UnitDependency> unit_dependencies() const
    {
        std::map<std::string, UnitDependency> dependencies;
        static const std::set<std::string> dependency_relations = {
            "calls", "constructs", "forwards_to", "references_function"
        };

        for (const auto& item : relationships_) {
            const CodeMapRelationship& relationship = item.second;
            if (dependency_relations.count(relationship.relationship) == 0)
                continue;

            const auto source_found = entities_.find(relationship.source_id);
            const auto target_found = entities_.find(relationship.target_id);
            if (source_found == entities_.end() ||
                    target_found == entities_.end()) {
                continue;
            }

            const CodeMapEntity& source = source_found->second;
            const CodeMapEntity& target = target_found->second;
            const std::string source_unit =
                source.kind == "compilation_unit"
                ? source.id : source.home_compilation_unit;
            const std::string& target_unit = target.home_compilation_unit;
            if (source_unit.empty() || target_unit.empty() ||
                    source_unit == target_unit) {
                continue;
            }

            const auto source_unit_found = entities_.find(source_unit);
            const auto target_unit_found = entities_.find(target_unit);
            if (source_unit_found == entities_.end() ||
                    target_unit_found == entities_.end()) {
                continue;
            }

            std::string key = source_unit;
            key += '\t';
            key += target_unit;
            key += '\t';
            key += relationship.relationship;
            UnitDependency& dependency = dependencies[key];
            dependency.source_unit = source_unit;
            dependency.target_unit = target_unit;
            dependency.source_path = source_unit_found->second.defining_path;
            dependency.target_path = target_unit_found->second.defining_path;
            dependency.relationship = relationship.relationship;
            dependency.edge_count += relationship.count;
            dependency.source_entities.insert(relationship.source_id);
        }

        std::vector<UnitDependency> output;
        output.reserve(dependencies.size());
        for (const auto& item : dependencies)
            output.push_back(item.second);
        return output;
    }

    bool write_entities(llvm::StringRef path, std::string& error) const
    {
        std::ofstream output(path.str());
        if (!output) {
            error = "cannot write " + path.str();
            return false;
        }
        output << "id\tkind\tname\tqualified_name\tnamespace"
            "\texpected_namespace\tnamespace_path_match\tpath"
            "\tline\tcompilation_unit\tmodule\tlint_tag"
            "\tsignature\tnoexcept\tparent_id\towner_type"
            "\ttrivial_destructor\tlinkage\tseen_in_tu_count\n";
        for (const auto& item : entities_) {
            const CodeMapEntity& entity = item.second;
            output << tsv_escape(entity.id) << '\t'
                   << tsv_escape(entity.kind) << '\t'
                   << tsv_escape(entity.name) << '\t'
                   << tsv_escape(entity.qualified_name) << '\t'
                   << tsv_escape(entity.namespace_name) << '\t'
                   << tsv_escape(entity.expected_namespace) << '\t'
                   << tsv_escape(entity.namespace_path_match) << '\t'
                   << tsv_escape(entity.defining_path) << '\t'
                   << entity.defining_line << '\t'
                   << tsv_escape(entity.home_compilation_unit) << '\t'
                   << tsv_escape(entity.module) << '\t'
                   << tsv_escape(entity.lint_tag) << '\t'
                   << tsv_escape(entity.signature) << '\t'
                   << tsv_escape(entity.nothrow) << '\t'
                   << tsv_escape(entity.parent_id) << '\t'
                   << tsv_escape(entity.owner_type) << '\t'
                   << tsv_escape(entity.trivial_destructor) << '\t'
                   << tsv_escape(entity.linkage) << '\t'
                   << entity.seen_in_units.size() << '\n';
        }
        return true;
    }

    bool write_relationships(llvm::StringRef path, std::string& error) const
    {
        std::ofstream output(path.str());
        if (!output) {
            error = "cannot write " + path.str();
            return false;
        }
        output << "source_id\ttarget_id\trelationship\tsource_path"
            "\tsource_line\tsource_column\tcount\n";
        for (const auto& item : relationships_) {
            const CodeMapRelationship& relationship = item.second;
            output << tsv_escape(relationship.source_id) << '\t'
                   << tsv_escape(relationship.target_id) << '\t'
                   << tsv_escape(relationship.relationship) << '\t'
                   << tsv_escape(relationship.source_path) << '\t'
                   << relationship.source_line << '\t'
                   << relationship.source_column << '\t'
                   << relationship.count << '\n';
        }
        return true;
    }

    bool write_dependencies(llvm::StringRef path, std::string& error) const
    {
        std::ofstream output(path.str());
        if (!output) {
            error = "cannot write " + path.str();
            return false;
        }
        output << "source_unit\ttarget_unit\tsource_path\ttarget_path"
            "\trelationship\tedge_count\tsource_entity_count\n";
        for (const UnitDependency& dependency : unit_dependencies()) {
            output << tsv_escape(dependency.source_unit) << '\t'
                   << tsv_escape(dependency.target_unit) << '\t'
                   << tsv_escape(dependency.source_path) << '\t'
                   << tsv_escape(dependency.target_path) << '\t'
                   << tsv_escape(dependency.relationship) << '\t'
                   << dependency.edge_count << '\t'
                   << dependency.source_entities.size() << '\n';
        }
        return true;
    }

    static void js_property(
        std::ostream& output,
        llvm::StringRef name,
        llvm::StringRef value,
        bool comma = true
    ) {
        output << '"' << name.str() << "\":\"" << js_escape(value) << '"';
        if (comma)
            output << ',';
    }

    bool write_data_js(llvm::StringRef path, std::string& error) const
    {
        std::ofstream output(path.str());
        if (!output) {
            error = "cannot write " + path.str();
            return false;
        }

        unsigned entrypoints = 0;
        unsigned abi_shims = 0;
        for (const auto& item : entities_) {
            entrypoints += item.second.lint_tag == "entrypoint";
            abi_shims += item.second.lint_tag == "abi_shim";
        }

        output << "window.CHARR_CODE_MAP={\n\"metadata\":{";
        output << "\"translation_unit_count\":";
        output << std::count_if(
            entities_.begin(), entities_.end(),
            [](const auto& item) {
                return item.second.kind == "compilation_unit";
            }
        );
        output << ",\"entrypoint_count\":" << entrypoints;
        output << ",\"abi_shim_count\":" << abi_shims;
        output << ",\"unresolved_call_count\":"
               << unresolved_callsites_.size() << "},\n";

        output << "\"entities\":[\n";
        bool first = true;
        for (const auto& item : entities_) {
            const CodeMapEntity& entity = item.second;
            if (!first)
                output << ",\n";
            first = false;
            output << '{';
            js_property(output, "id", entity.id);
            js_property(output, "kind", entity.kind);
            js_property(output, "name", entity.name);
            js_property(output, "qualified_name", entity.qualified_name);
            js_property(output, "namespace", entity.namespace_name);
            js_property(output, "expected_namespace", entity.expected_namespace);
            js_property(output, "namespace_path_match", entity.namespace_path_match);
            js_property(output, "path", entity.defining_path);
            output << "\"line\":" << entity.defining_line << ',';
            js_property(output, "compilation_unit", entity.home_compilation_unit);
            js_property(output, "module", entity.module);
            js_property(output, "lint_tag", entity.lint_tag);
            js_property(output, "signature", entity.signature);
            js_property(output, "noexcept", entity.nothrow);
            js_property(output, "parent_id", entity.parent_id);
            js_property(output, "owner_type", entity.owner_type);
            js_property(output, "trivial_destructor", entity.trivial_destructor);
            js_property(output, "linkage", entity.linkage);
            output << "\"seen_in_tu_count\":"
                   << entity.seen_in_units.size() << '}';
        }
        output << "\n],\n\"relationships\":[\n";
        first = true;
        for (const auto& item : relationships_) {
            const CodeMapRelationship& relationship = item.second;
            if (!first)
                output << ",\n";
            first = false;
            output << '{';
            js_property(output, "source_id", relationship.source_id);
            js_property(output, "target_id", relationship.target_id);
            js_property(output, "relationship", relationship.relationship);
            js_property(output, "source_path", relationship.source_path);
            output << "\"source_line\":" << relationship.source_line << ',';
            output << "\"source_column\":" << relationship.source_column << ',';
            output << "\"count\":" << relationship.count << '}';
        }
        output << "\n],\n\"unit_dependencies\":[\n";
        first = true;
        for (const UnitDependency& dependency : unit_dependencies()) {
            if (!first)
                output << ",\n";
            first = false;
            output << '{';
            js_property(output, "source_unit", dependency.source_unit);
            js_property(output, "target_unit", dependency.target_unit);
            js_property(output, "source_path", dependency.source_path);
            js_property(output, "target_path", dependency.target_path);
            js_property(output, "relationship", dependency.relationship);
            output << "\"edge_count\":" << dependency.edge_count << ',';
            output << "\"source_entity_count\":"
                   << dependency.source_entities.size() << '}';
        }
        output << "\n]};\n";
        return true;
    }

public:
    void add_compilation_unit(llvm::StringRef path)
    {
        CodeMapEntity entity;
        entity.id = "unit:" + path.str();
        entity.kind = "compilation_unit";
        entity.name = llvm::sys::path::filename(path).str();
        entity.qualified_name = path.str();
        entity.defining_path = path.str();
        entity.home_compilation_unit = entity.id;
        entity.module = module_for_path(path);
        entity.definition_rank = 2;
        entity.seen_in_units.insert(entity.id);
        merge_entity(std::move(entity));
    }

    std::string ensure_record(
        const clang::RecordDecl& input,
        clang::ASTContext& context,
        llvm::StringRef current_unit
    ) {
        const clang::RecordDecl* family = record_family(input);
        const clang::RecordDecl* definition = family->getDefinition();
        const clang::RecordDecl* declaration = definition == nullptr
            ? family : definition;
        const clang::SourceManager& source_manager = context.getSourceManager();
        const SourcePoint location = source_point(*declaration, source_manager);
        const std::string qualified = declaration->getQualifiedNameAsString();
        const std::string id = declaration_id(
            *family, "record", qualified, "", location
        );
        const bool owned = source_manager.isWrittenInMainFile(
            source_manager.getSpellingLoc(declaration->getLocation())
        ) || is_code_map_owned_path(location.path);

        CodeMapEntity entity;
        entity.id = id;
        const llvm::StringRef tag = declaration->getKindName();
        if (declaration->isUnion())
            entity.kind = owned ? "union" : "external_union";
        else if (declaration->isStruct())
            entity.kind = owned ? "struct" : "external_struct";
        else
            entity.kind = owned ? "class" : "external_class";
        entity.name = declaration->getNameAsString();
        if (entity.name.empty())
            entity.name = std::string("(anonymous ") + tag.str() + ')';
        entity.qualified_name = qualified.empty() ? entity.name : qualified;
        entity.namespace_name = namespace_for_decl(*declaration);
        entity.defining_path = location.path;
        entity.defining_line = location.line;
        entity.module = owned ? module_for_path(location.path) : "external";
        if (owned) {
            entity.expected_namespace =
                expected_namespace_for_path(location.path);
            if (!entity.expected_namespace.empty()) {
                entity.namespace_path_match =
                    entity.namespace_name == entity.expected_namespace ||
                    llvm::StringRef(entity.namespace_name).starts_with(
                        entity.expected_namespace + "::"
                    ) ? "true" : "false";
            }
        }
        if (is_cpp_path(location.path))
            entity.home_compilation_unit = "unit:" + location.path;
        entity.linkage = linkage_name(declaration->getFormalLinkage());
        entity.owner_type = is_owner_type(
            context.getRecordType(const_cast<clang::RecordDecl*>(declaration))
        ) ? "true" : "false";
        if (const auto* cxx =
                llvm::dyn_cast<clang::CXXRecordDecl>(declaration)) {
            if (cxx->hasDefinition()) {
                entity.trivial_destructor =
                    cxx->hasTrivialDestructor() ? "true" : "false";
            }
        }
        entity.definition_rank = definition == nullptr ? 1 : 2;
        entity.seen_in_units.insert(current_unit.str());
        merge_entity(std::move(entity));
        return id;
    }

    std::string ensure_function(
        const clang::FunctionDecl& input,
        clang::ASTContext& context,
        llvm::StringRef current_unit
    ) {
        const clang::FunctionDecl* family = function_family(input);
        const bool input_is_family_definition =
            input.doesThisDeclarationHaveABody() &&
            input.getCanonicalDecl() == family;
        const clang::FunctionDecl* definition = input_is_family_definition
            ? &input : family->getDefinition();
        const clang::FunctionDecl* declaration = definition == nullptr
            ? family : definition;
        const Role role = annotation_role(*declaration);
        const clang::SourceManager& source_manager = context.getSourceManager();
        SourcePoint location = source_point(
            *declaration, source_manager, role == Role::abi_shim
        );
        if (input_is_family_definition && role == Role::abi_shim &&
                current_unit.starts_with("unit:")) {
            location.path = current_unit.drop_front(5).str();
        }
        const std::string signature =
            family->getType().getCanonicalType().getAsString();
        const std::string qualified = family->getQualifiedNameAsString();
        const std::string id = declaration_id(
            *family, "function", qualified, signature, location
        );
        const bool owned = is_code_map_owned_path(location.path) ||
            source_manager.isWrittenInMainFile(
                source_manager.getSpellingLoc(declaration->getLocation())
            );

        CodeMapEntity entity;
        entity.id = id;
        if (llvm::isa<clang::CXXConstructorDecl>(declaration))
            entity.kind = owned ? "constructor" : "external_constructor";
        else if (llvm::isa<clang::CXXDestructorDecl>(declaration))
            entity.kind = owned ? "destructor" : "external_destructor";
        else if (llvm::isa<clang::CXXMethodDecl>(declaration))
            entity.kind = owned ? "method" : "external_method";
        else
            entity.kind = owned ? "function" : "external_function";
        entity.name = declaration->getNameAsString();
        entity.qualified_name = qualified;
        entity.namespace_name = namespace_for_decl(*declaration);
        entity.defining_path = location.path;
        entity.defining_line = location.line;
        entity.module = owned ? module_for_path(location.path) : "external";
        if (owned) {
            entity.expected_namespace =
                expected_namespace_for_path(location.path);
            if (!entity.expected_namespace.empty()) {
                entity.namespace_path_match =
                    entity.namespace_name == entity.expected_namespace ||
                    llvm::StringRef(entity.namespace_name).starts_with(
                        entity.expected_namespace + "::"
                    ) ? "true" : "false";
            }
        }
        if (is_cpp_path(location.path))
            entity.home_compilation_unit = "unit:" + location.path;
        entity.lint_tag = owned ? role_tag(role) : "";
        entity.signature = signature;
        entity.nothrow = is_nothrow(*declaration) ? "true" : "false";
        entity.linkage = linkage_name(declaration->getFormalLinkage());
        if (owned) {
            if (const auto* method =
                    llvm::dyn_cast<clang::CXXMethodDecl>(declaration)) {
                if (!method->getParent()->isLambda()) {
                    entity.parent_id = ensure_record(
                        *method->getParent(), context, current_unit
                    );
                }
            }
        }
        entity.definition_rank = definition == nullptr ? 1 : 2;
        entity.seen_in_units.insert(current_unit.str());
        merge_entity(std::move(entity));
        return id;
    }

    void add_relationship(
        llvm::StringRef source_id,
        llvm::StringRef target_id,
        llvm::StringRef relationship,
        const SourcePoint& location
    ) {
        if (source_id.empty() || target_id.empty())
            return;
        const std::string key = relationship_key(
            source_id, target_id, relationship, location
        );
        CodeMapRelationship edge;
        edge.source_id = source_id.str();
        edge.target_id = target_id.str();
        edge.relationship = relationship.str();
        edge.source_path = location.path;
        edge.source_line = location.line;
        edge.source_column = location.column;
        relationships_.insert(std::make_pair(key, std::move(edge)));
    }

    void add_unresolved_call(
        llvm::StringRef caller_id,
        const SourcePoint& location
    ) {
        std::string key = caller_id.str();
        key += '\t';
        key += location.path;
        key += '\t';
        key += std::to_string(location.line);
        key += '\t';
        key += std::to_string(location.column);
        unresolved_callsites_.insert(std::move(key));
    }

    bool write(llvm::StringRef directory, std::string& error)
    {
        finalize_relationships();
        std::error_code filesystem_error =
            llvm::sys::fs::create_directories(directory);
        if (filesystem_error) {
            error = "cannot create code-map directory '" + directory.str() +
                "': " + filesystem_error.message();
            return false;
        }

        llvm::SmallString<256> path(directory);
        llvm::sys::path::append(path, "entities.tsv");
        if (!write_entities(path, error))
            return false;
        path = directory;
        llvm::sys::path::append(path, "relationships.tsv");
        if (!write_relationships(path, error))
            return false;
        path = directory;
        llvm::sys::path::append(path, "unit_dependencies.tsv");
        if (!write_dependencies(path, error))
            return false;
        path = directory;
        llvm::sys::path::append(path, "data.js");
        return write_data_js(path, error);
    }
};

class CodeMapVisitor :
    public clang::RecursiveASTVisitor<CodeMapVisitor> {
private:
    clang::ASTContext& context_;
    const ExternalEffects& effects_;
    CodeMap& code_map_;
    std::string unit_path_;
    std::string unit_id_;

    bool owned_location(clang::SourceLocation location) const
    {
        const clang::SourceManager& source_manager =
            context_.getSourceManager();
        location = source_manager.getSpellingLoc(location);
        if (location.isInvalid() || source_manager.isInSystemHeader(location))
            return false;
        return source_manager.isWrittenInMainFile(location) ||
            is_code_map_owned_path(
                source_point(location, source_manager).path
            );
    }

    void add_type_uses(
        llvm::StringRef source_id,
        clang::QualType type,
        clang::SourceLocation location
    ) {
        llvm::SmallPtrSet<const clang::Type*, 16> visited;
        add_type_uses(source_id, type, location, visited);
    }

    void add_type_uses(
        llvm::StringRef source_id,
        clang::QualType type,
        clang::SourceLocation location,
        llvm::SmallPtrSetImpl<const clang::Type*>& visited
    ) {
        if (type.isNull())
            return;
        type = type.getCanonicalType();
        const clang::Type* raw = type.getTypePtr();
        if (!visited.insert(raw).second)
            return;

        if (const auto* pointer = raw->getAs<clang::PointerType>()) {
            add_type_uses(source_id, pointer->getPointeeType(), location, visited);
            return;
        }
        if (const auto* reference = raw->getAs<clang::ReferenceType>()) {
            add_type_uses(source_id, reference->getPointeeType(), location, visited);
            return;
        }
        if (const auto* array = llvm::dyn_cast<clang::ArrayType>(raw)) {
            add_type_uses(source_id, array->getElementType(), location, visited);
            return;
        }
        if (const auto* member = llvm::dyn_cast<clang::MemberPointerType>(raw)) {
            add_type_uses(source_id, member->getPointeeType(), location, visited);
            return;
        }
        if (const auto* function = llvm::dyn_cast<clang::FunctionProtoType>(raw)) {
            add_type_uses(source_id, function->getReturnType(), location, visited);
            for (clang::QualType parameter : function->param_types())
                add_type_uses(source_id, parameter, location, visited);
            return;
        }

        const clang::RecordType* record_type = raw->getAs<clang::RecordType>();
        if (record_type == nullptr)
            return;
        const clang::RecordDecl* record = record_type->getDecl();
        const clang::RecordDecl* definition = record->getDefinition();
        if (definition != nullptr)
            record = definition;
        if (owned_location(record->getLocation())) {
            const std::string target_id = code_map_.ensure_record(
                *record, context_, unit_id_
            );
            if (target_id != source_id) {
                code_map_.add_relationship(
                    source_id, target_id, "uses_type",
                    source_point(location, context_.getSourceManager())
                );
            }
        }

        const auto* specialization =
            llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        if (specialization == nullptr)
            return;
        for (const clang::TemplateArgument& argument :
                specialization->getTemplateArgs().asArray()) {
            if (argument.getKind() == clang::TemplateArgument::Type) {
                add_type_uses(
                    source_id, argument.getAsType(), location, visited
                );
            }
        }
    }

    const clang::FunctionDecl* enclosing_function(
        const clang::Stmt& statement
    ) const {
        llvm::SmallVector<clang::DynTypedNode, 16> pending;
        llvm::SmallPtrSet<const void*, 32> visited;
        pending.push_back(clang::DynTypedNode::create(statement));
        while (!pending.empty()) {
            const clang::DynTypedNode current = pending.pop_back_val();
            const void* memo = current.getMemoizationData();
            if (memo != nullptr && !visited.insert(memo).second)
                continue;
            for (const clang::DynTypedNode& parent :
                    context_.getParents(current)) {
                if (const auto* function =
                        parent.get<clang::FunctionDecl>()) {
                    const auto* method =
                        llvm::dyn_cast<clang::CXXMethodDecl>(function);
                    if (method == nullptr || !method->getParent()->isLambda())
                        return function;
                }
                pending.push_back(parent);
            }
        }
        return nullptr;
    }

    bool is_direct_call_reference(
        const clang::DeclRefExpr& reference,
        const clang::FunctionDecl& function
    ) const {
        llvm::SmallVector<clang::DynTypedNode, 12> pending;
        llvm::SmallPtrSet<const void*, 24> visited;
        pending.push_back(clang::DynTypedNode::create(reference));
        while (!pending.empty()) {
            const clang::DynTypedNode current = pending.pop_back_val();
            const void* memo = current.getMemoizationData();
            if (memo != nullptr && !visited.insert(memo).second)
                continue;
            for (const clang::DynTypedNode& parent :
                    context_.getParents(current)) {
                if (const auto* call = parent.get<clang::CallExpr>()) {
                    const clang::FunctionDecl* callee =
                        resolve_direct_callee(*call);
                    if (callee != nullptr &&
                            function_family(*callee) ==
                                function_family(function) &&
                            is_descendant_of(
                                context_, reference, *call->getCallee()
                            )) {
                        return true;
                    }
                    return false;
                }
                if (parent.get<clang::FunctionDecl>() != nullptr)
                    return false;
                pending.push_back(parent);
            }
        }
        return false;
    }

public:
    CodeMapVisitor(
        clang::ASTContext& context,
        const ExternalEffects& effects,
        CodeMap& code_map,
        llvm::StringRef unit_path
    ) : context_(context), effects_(effects), code_map_(code_map),
        unit_path_(unit_path), unit_id_("unit:" + unit_path.str())
    {
        code_map_.add_compilation_unit(unit_path_);
    }

    bool VisitRecordDecl(clang::RecordDecl* record)
    {
        if (!record->isThisDeclarationADefinition() || record->isImplicit() ||
                record->getDefinition() != record ||
                (llvm::isa<clang::CXXRecordDecl>(record) &&
                 llvm::cast<clang::CXXRecordDecl>(record)->isLambda()) ||
                !owned_location(record->getLocation())) {
            return true;
        }

        const std::string record_id =
            code_map_.ensure_record(*record, context_, unit_id_);
        for (const clang::FieldDecl* field : record->fields()) {
            add_type_uses(record_id, field->getType(), field->getLocation());
        }
        if (const auto* cxx = llvm::dyn_cast<clang::CXXRecordDecl>(record)) {
            for (const clang::CXXBaseSpecifier& base : cxx->bases()) {
                add_type_uses(record_id, base.getType(), base.getBeginLoc());
                const clang::CXXRecordDecl* base_record =
                    base.getType()->getAsCXXRecordDecl();
                if (base_record != nullptr &&
                        owned_location(base_record->getLocation())) {
                    const std::string base_id = code_map_.ensure_record(
                        *base_record, context_, unit_id_
                    );
                    code_map_.add_relationship(
                        record_id, base_id, "inherits",
                        source_point(
                            base.getBeginLoc(), context_.getSourceManager()
                        )
                    );
                }
            }
        }
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* function)
    {
        if (!function->doesThisDeclarationHaveABody() ||
                function->isImplicit() || function->isDefaulted() ||
                function->isDeleted()) {
            return true;
        }
        if (const auto* method =
                llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
            if (method->getParent()->isLambda())
                return true;
        }
        if (!owned_location(function->getLocation()) &&
                annotation_role(*function) == Role::none) {
            return true;
        }

        const std::string function_id =
            code_map_.ensure_function(*function, context_, unit_id_);
        add_type_uses(
            function_id, function->getReturnType(), function->getLocation()
        );
        for (const clang::ParmVarDecl* parameter : function->parameters()) {
            add_type_uses(
                function_id, parameter->getType(), parameter->getLocation()
            );
        }

        BodyVisitor body(context_, effects_);
        if (const auto* constructor =
                llvm::dyn_cast<clang::CXXConstructorDecl>(function)) {
            for (const clang::CXXCtorInitializer* initializer :
                    constructor->inits()) {
                body.TraverseStmt(
                    const_cast<clang::Expr*>(initializer->getInit())
                );
            }
        }
        body.TraverseStmt(const_cast<clang::Stmt*>(function->getBody()));
        for (const clang::VarDecl* local : body.locals)
            add_type_uses(function_id, local->getType(), local->getLocation());

        for (const CallRecord& call : body.calls) {
            const SourcePoint location = source_point(
                call.expression->getExprLoc(), context_.getSourceManager()
            );
            if (call.callee == nullptr) {
                code_map_.add_unresolved_call(function_id, location);
                continue;
            }
            if (call.callee->isImplicit() || call.callee->isDefaulted() ||
                    call.callee->isDeleted()) {
                continue;
            }
            if (const auto* method =
                    llvm::dyn_cast<clang::CXXMethodDecl>(call.callee)) {
                if (method->getParent()->isLambda())
                    continue;
            }
            const std::string callee_id = code_map_.ensure_function(
                *call.callee, context_, unit_id_
            );
            llvm::StringRef relationship = call.construction
                ? "constructs" : "calls";
            if (!call.construction &&
                    annotation_role(*function) == Role::abi_shim &&
                    annotation_role(*call.callee) == Role::entrypoint) {
                relationship = "forwards_to";
            }
            code_map_.add_relationship(
                function_id, callee_id, relationship, location
            );
        }
        return true;
    }

    bool VisitDeclRefExpr(clang::DeclRefExpr* reference)
    {
        const auto* referenced =
            llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl());
        if (referenced == nullptr || referenced->isImplicit() ||
                referenced->isDefaulted() || referenced->isDeleted() ||
                is_direct_call_reference(*reference, *referenced)) {
            return true;
        }
        if (const auto* method =
                llvm::dyn_cast<clang::CXXMethodDecl>(referenced)) {
            if (method->getParent()->isLambda())
                return true;
        }

        const clang::SourceManager& source_manager =
            context_.getSourceManager();
        const SourcePoint location = source_point(
            reference->getExprLoc(), source_manager
        );
        std::string source_id;
        if (const clang::FunctionDecl* source =
                enclosing_function(*reference)) {
            if (!owned_location(source->getLocation()))
                return true;
            source_id = code_map_.ensure_function(
                *source, context_, unit_id_
            );
        }
        else {
            const clang::SourceLocation spelling =
                source_manager.getSpellingLoc(reference->getExprLoc());
            if (!source_manager.isWrittenInMainFile(spelling))
                return true;
            source_id = unit_id_;
        }

        const std::string target_id = code_map_.ensure_function(
            *referenced, context_, unit_id_
        );
        code_map_.add_relationship(
            source_id, target_id, "references_function", location
        );
        return true;
    }
};

class TranslationUnitVisitor :
    public clang::RecursiveASTVisitor<TranslationUnitVisitor> {
private:
    clang::ASTContext& context_;
    FunctionChecker checker_;

public:
    TranslationUnitVisitor(
        clang::ASTContext& context,
        Reporter& reporter,
        const ExternalEffects& effects
    ) : context_(context), checker_(context, reporter, effects) {}

    bool shouldVisitTemplateInstantiations() const
    {
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* function)
    {
        if (!function->doesThisDeclarationHaveABody() ||
                function->isDefaulted() || function->isDeleted()) {
            return true;
        }
        if (function->isImplicit() &&
                annotation_role(*function) != Role::trusted_unwind) {
            return true;
        }
        if (const auto* method =
                llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
            if (method->getParent()->isLambda())
                return true;
        }

        const clang::SourceManager& source_manager =
            context_.getSourceManager();
        const clang::SourceLocation location =
            source_manager.getSpellingLoc(function->getLocation());
        if (location.isInvalid() || source_manager.isInSystemHeader(location))
            return true;
        if (!is_charr_owned(*function, source_manager))
            return true;
        const clang::SourceLocation expansion =
            source_manager.getExpansionLoc(function->getLocation());
        if (main_files_only &&
                !source_manager.isWrittenInMainFile(location) &&
                (expansion.isInvalid() ||
                 !source_manager.isWrittenInMainFile(expansion))) {
            return true;
        }

        checker_.check(*function);
        return true;
    }
};

class LintConsumer : public clang::ASTConsumer {
private:
    Reporter& reporter_;
    const ExternalEffects& effects_;
    CodeMap* code_map_;
    std::string unit_path_;

public:
    LintConsumer(
        Reporter& reporter,
        const ExternalEffects& effects,
        CodeMap* code_map,
        llvm::StringRef unit_path
    ) : reporter_(reporter), effects_(effects), code_map_(code_map),
        unit_path_(normalize_path(unit_path)) {}

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        if (code_map_ != nullptr) {
            const clang::SourceManager& source_manager =
                context.getSourceManager();
            const clang::FileEntry* main_file =
                source_manager.getFileEntryForID(
                    source_manager.getMainFileID()
                );
            std::string unit_path = unit_path_;
            if (main_file != nullptr) {
                llvm::StringRef path = main_file->tryGetRealPathName();
                if (!path.empty())
                    unit_path = normalize_path(path);
            }
            CodeMapVisitor map_visitor(
                context, effects_, *code_map_, unit_path
            );
            map_visitor.TraverseDecl(context.getTranslationUnitDecl());
        }
        TranslationUnitVisitor visitor(context, reporter_, effects_);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }
};

class LintAction : public clang::ASTFrontendAction {
private:
    Reporter& reporter_;
    const ExternalEffects& effects_;
    CodeMap* code_map_;

public:
    LintAction(
        Reporter& reporter,
        const ExternalEffects& effects,
        CodeMap* code_map
    ) : reporter_(reporter), effects_(effects), code_map_(code_map) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&,
        llvm::StringRef input_file
    ) override {
        return std::make_unique<LintConsumer>(
            reporter_, effects_, code_map_, input_file
        );
    }
};

class LintActionFactory : public clang::tooling::FrontendActionFactory {
private:
    Reporter& reporter_;
    const ExternalEffects& effects_;
    CodeMap* code_map_;

public:
    LintActionFactory(
        Reporter& reporter,
        const ExternalEffects& effects,
        CodeMap* code_map
    ) : reporter_(reporter), effects_(effects), code_map_(code_map) {}

    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<LintAction>(
            reporter_, effects_, code_map_
        );
    }
};

} // namespace

int main(int argc, const char** argv)
{
    auto parser = clang::tooling::CommonOptionsParser::create(
        argc, argv, lint_category
    );
    if (!parser) {
        llvm::errs() << parser.takeError();
        return 2;
    }

    ExternalEffects effects;
    std::string catalog_error;
    if (!effects.load_manifest(effects_path, catalog_error) ||
            !effects.load_overrides(
                effect_overrides_path, catalog_error)) {
        llvm::errs() << catalog_error << '\n';
        return 2;
    }

    Reporter reporter;
    CodeMap code_map;
    clang::tooling::ClangTool tool(
        parser->getCompilations(), parser->getSourcePathList()
    );
    tool.appendArgumentsAdjuster(
        clang::tooling::getInsertArgumentAdjuster(
            "-DCHARR_LINT=1",
            clang::tooling::ArgumentInsertPosition::BEGIN
        )
    );
    LintActionFactory factory(
        reporter, effects, code_map_dir.empty() ? nullptr : &code_map
    );
    const int tooling_result = tool.run(&factory);
    if (tooling_result != 0)
        return tooling_result;
    if (reporter.integrity_errors() != 0)
        return 2;
    if (dump_external_calls)
        reporter.print_external_calls();
    if (!write_effects_manifest.empty()) {
        std::string manifest_error;
        if (!reporter.write_external_calls(
                write_effects_manifest, effects.manifest(),
                manifest_error)) {
            llvm::errs() << manifest_error << '\n';
            return 2;
        }
    }
    if (!code_map_dir.empty()) {
        std::string code_map_error;
        if (!code_map.write(code_map_dir, code_map_error)) {
            llvm::errs() << code_map_error << '\n';
            return 2;
        }
    }
    if (reporter.errors() != 0 && !audit_mode)
        return 1;
    return 0;
}
