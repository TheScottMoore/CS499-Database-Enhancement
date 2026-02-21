/*
  ENHANCED_Animal_Shelter.cpp
  ---------------------------
  This is the C++ version of the AnimalShelter CRUD layer

  Added in-memory caching/indexing to speed up repeated “dashboard” queries

*/

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/update.hpp>
#include <mongocxx/uri.hpp>

namespace aac {

// Simple config bucket. If env vars exist, reads them. Otherwise defaults are used.
struct MongoConfig {
    std::string uri;                  //"mongodb://localhost:27017"
    std::string database = "AAC";     
    std::string collection = "animals";
};

class AnimalShelter {
public:
    explicit AnimalShelter(MongoConfig cfg)
        : cfg_(std::move(cfg)),
          client_(mongocxx::uri{cfg_.uri}),
          db_(client_[cfg_.database]),
          col_(db_[cfg_.collection]) {}

    // Default constructor uses environment vars if present.
    AnimalShelter()
        : AnimalShelter(load_from_env()) {}

    
    // CRUD
   
    // Insert one document. Returns true if MongoDB gave us a result object back.
    bool create(const bsoncxx::document::view& doc) {
        if (doc.empty()) {
            throw std::invalid_argument("create(): document is empty");
        }

        auto res = col_.insert_one(doc);
        const bool ok = static_cast<bool>(res);

        if (ok) {
            const bsoncxx::oid new_id = res->inserted_id().get_oid().value;
            maybe_cache_insert(new_id);
        }

        return ok;
    }

    // Read docs matching a filter.
    std::vector<bsoncxx::document::value> read(const bsoncxx::document::view& filter,
                                              std::optional<int64_t> limit = std::nullopt) {
        if (filter.empty()) {
            throw std::invalid_argument("read(): filter is empty");
        }

        mongocxx::options::find opts;
        if (limit && *limit > 0) {
            opts.limit(*limit);
        }

        std::vector<bsoncxx::document::value> out;
        auto cursor = col_.find(filter, opts);
        for (auto&& doc : cursor) {
            out.emplace_back(bsoncxx::document::value{doc}); // own the doc memory
        }
        return out;
    }

    // Update many docs using $set(update_values).
    // Returns how many were modified.
    std::int64_t update(const bsoncxx::document::view& filter,
                        const bsoncxx::document::view& update_values) {
        if (filter.empty()) {
            throw std::invalid_argument("update(): filter is empty");
        }
        if (update_values.empty()) {
            throw std::invalid_argument("update(): update_values is empty");
        }

        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        document update_doc;
        update_doc.append(kvp("$set", update_values));

        auto res = col_.update_many(filter, update_doc.view());
        const auto modified = res ? res->modified_count() : 0;

        if (cache_enabled_) {
            mark_cache_stale();
        }
        return modified;
    }

    // Delete many docs matching filter. Returns count deleted.
    std::int64_t remove(const bsoncxx::document::view& filter) {
        if (filter.empty()) {
            throw std::invalid_argument("remove(): filter is empty");
        }

        auto res = col_.delete_many(filter);
        const auto deleted = res ? res->deleted_count() : 0;

        if (cache_enabled_ && deleted > 0) {
            mark_cache_stale();
        }
        return deleted;
    }

    // Count matching docs (server-side)
    std::int64_t count(const bsoncxx::document::view& filter) {
        return col_.count_documents(filter);
    }

    // Cache controls + "faster reads"
  
    // Turn cache on/off. If on, build it immediately.
    void enable_cache(bool enabled) {
        cache_enabled_ = enabled;
        if (cache_enabled_) {
            rebuild_cache();
        } else {
            clear_cache();
        }
    }

    // Fast path for equality filters (if cache enabled).
    // If cache doesn't have this field indexed, it falls back to a normal query.
    std::vector<bsoncxx::document::value> read_by_field_eq(const std::string& field,
                                                          const std::string& value,
                                                          std::optional<int64_t> limit = std::nullopt) {
        if (!cache_enabled_) {
            return read(make_eq_filter(field, value).view(), limit);
        }

        ensure_cache_fresh();

        std::vector<bsoncxx::oid> ids;
        {
            std::shared_lock lk(cache_mtx_);
            auto it = eq_index_.find(field);
            if (it == eq_index_.end()) {
                return read(make_eq_filter(field, value).view(), limit);
            }

            auto range = it->second.equal_range(value);
            for (auto i = range.first; i != range.second; ++i) {
                ids.push_back(i->second);
                if (limit && static_cast<int64_t>(ids.size()) >= *limit) break;
            }
        }

        if (ids.empty()) return {};
        return read(make_in_filter("_id", ids).view(), limit);
    }

    // Range query for age in weeks using binary search on a sorted vector if cache is enabled.
    std::vector<bsoncxx::document::value> read_by_age_range(int min_weeks, int max_weeks,
                                                           std::optional<int64_t> limit = std::nullopt) {
        if (min_weeks > max_weeks) std::swap(min_weeks, max_weeks);

        if (!cache_enabled_) {
            return read(make_age_range_filter(min_weeks, max_weeks).view(), limit);
        }

        ensure_cache_fresh();

        std::vector<bsoncxx::oid> ids;
        {
            std::shared_lock lk(cache_mtx_);

            auto lb = std::lower_bound(
                age_sorted_.begin(), age_sorted_.end(), min_weeks,
                [](const AgeIndexRow& row, int v) { return row.age_weeks < v; }
            );

            auto ub = std::upper_bound(
                age_sorted_.begin(), age_sorted_.end(), max_weeks,
                [](int v, const AgeIndexRow& row) { return v < row.age_weeks; }
            );

            for (auto it = lb; it != ub; ++it) {
                ids.push_back(it->id);
                if (limit && static_cast<int64_t>(ids.size()) >= *limit) break;
            }
        }

        if (ids.empty()) return {};
        return read(make_in_filter("_id", ids).view(), limit);
    }

    MongoConfig config() const { return cfg_; }

private:
    struct AgeIndexRow {
        int age_weeks;
        bsoncxx::oid id;
    };

    // Driver instance must exist for the the lifetime of the program.
    inline static mongocxx::instance instance_{};

    MongoConfig cfg_;
    mongocxx::client client_;
    mongocxx::database db_;
    mongocxx::collection col_;

    // Cache:
    //   field -> (value -> _id)
    using MultiMap = std::unordered_multimap<std::string, bsoncxx::oid>;
    std::unordered_map<std::string, MultiMap> eq_index_;
    std::vector<AgeIndexRow> age_sorted_;

    bool cache_enabled_ = false;
    mutable std::shared_mutex cache_mtx_;
    bool cache_stale_ = false;

    static MongoConfig load_from_env() {
        MongoConfig cfg;

        if (const char* uri = std::getenv("AAC_MONGO_URI")) cfg.uri = uri;
        if (cfg.uri.empty()) {
            // For local MongoDB installs without auth:
            cfg.uri = "mongodb://localhost:27017";
        }

        if (const char* db = std::getenv("AAC_DB")) cfg.database = db;
        if (const char* col = std::getenv("AAC_COLLECTION")) cfg.collection = col;

        return cfg;
    }

    void clear_cache() {
        std::unique_lock lk(cache_mtx_);
        eq_index_.clear();
        age_sorted_.clear();
        cache_stale_ = false;
    }

    void mark_cache_stale() {
        std::unique_lock lk(cache_mtx_);
        cache_stale_ = true;
    }

    void ensure_cache_fresh() {
        bool stale = false;
        {
            std::shared_lock lk(cache_mtx_);
            stale = cache_stale_;
        }
        if (stale) rebuild_cache();
    }

    // Helper: bsoncxx string_view -> std::string
    static std::string sv_to_string(const bsoncxx::stdx::string_view& sv) {
        return std::string(sv.data(), sv.size());
    }

    // Cache rebuild:
    // Grab only the relevant fields, then builds:
    //  - hash indexes for equality lookups
    //  - sorted vector for age range queries
    void rebuild_cache() {
        std::unordered_map<std::string, MultiMap> new_eq;
        std::vector<AgeIndexRow> new_age;

        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        document proj;
        proj.append(kvp("_id", 1));
        proj.append(kvp("animal_type", 1));
        proj.append(kvp("breed", 1));
        proj.append(kvp("outcome_type", 1));
        proj.append(kvp("sex_upon_outcome", 1));
        proj.append(kvp("age_upon_outcome_in_weeks", 1));

        mongocxx::options::find opts;
        opts.projection(proj.view());

        auto cursor = col_.find({}, opts);

        for (auto&& doc : cursor) {
            auto id_el = doc["_id"];
            if (!id_el || id_el.type() != bsoncxx::type::k_oid) continue;
            bsoncxx::oid id = id_el.get_oid().value;

            index_eq_field(new_eq, "animal_type", doc, id);
            index_eq_field(new_eq, "breed", doc, id);
            index_eq_field(new_eq, "outcome_type", doc, id);
            index_eq_field(new_eq, "sex_upon_outcome", doc, id);

            auto age_el = doc["age_upon_outcome_in_weeks"];
            if (age_el) {
                if (age_el.type() == bsoncxx::type::k_int32) {
                    new_age.push_back({age_el.get_int32().value, id});
                } else if (age_el.type() == bsoncxx::type::k_int64) {
                    new_age.push_back({static_cast<int>(age_el.get_int64().value), id});
                } else if (age_el.type() == bsoncxx::type::k_double) {
                    new_age.push_back({static_cast<int>(age_el.get_double().value), id});
                }
            }
        }

        std::sort(new_age.begin(), new_age.end(),
                  [](const AgeIndexRow& a, const AgeIndexRow& b) {
                      return a.age_weeks < b.age_weeks;
                  });

        {
            std::unique_lock lk(cache_mtx_);
            eq_index_.swap(new_eq);
            age_sorted_.swap(new_age);
            cache_stale_ = false;
        }
    }


    // attempt get_string() and catch if it isn't a string.
    static void index_eq_field(std::unordered_map<std::string, MultiMap>& new_eq,
                              const std::string& field,
                              const bsoncxx::document::view& doc,
                              const bsoncxx::oid& id) {
        auto el = doc[field];
        if (!el) return;

        // Simple case: element is a string.
        try {
            const auto sv = el.get_string().value;
            new_eq[field].emplace(sv_to_string(sv), id);
            return;
        } catch (...) {
            // Not a string, move on.
        }

        // In case it's an array, index any string entries.
        try {
            auto arr = el.get_array().value;
            for (auto&& v : arr) {
                try {
                    const auto sv = v.get_string().value;
                    new_eq[field].emplace(sv_to_string(sv), id);
                } catch (...) {
                    // skip non-strings
                }
            }
        } catch (...) {
            // Not an array either; ignore it.
        }
    }

    // Mark cache stale after inserts
    void maybe_cache_insert(const bsoncxx::oid& /*new_id*/) {
        if (cache_enabled_) mark_cache_stale();
    }

    // Query builder helpers

    static bsoncxx::builder::basic::document make_eq_filter(const std::string& field,
                                                           const std::string& value) {
        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        document d;
        d.append(kvp(field, value));
        return d;
    }

    static bsoncxx::builder::basic::document make_in_filter(const std::string& field,
                                                            const std::vector<bsoncxx::oid>& ids) {
        using bsoncxx::builder::basic::array;
        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        array arr;
        for (const auto& id : ids) {
            arr.append(id);
        }

        document in;
        in.append(kvp("$in", arr));

        document d;
        d.append(kvp(field, in));
        return d;
    }

    static bsoncxx::builder::basic::document make_age_range_filter(int min_weeks, int max_weeks) {
        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        document range;
        range.append(kvp("$gte", min_weeks));
        range.append(kvp("$lte", max_weeks));

        document d;
        d.append(kvp("age_upon_outcome_in_weeks", range));
        return d;
    }
};

} 

