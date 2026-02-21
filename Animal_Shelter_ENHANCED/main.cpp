//main.cpp

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>

#include "ENHANCED_Animal_Shelter.cpp"

int main() {
    try {

        aac::AnimalShelter shelter;

        shelter.enable_cache(true);

        using bsoncxx::builder::basic::document;
        using bsoncxx::builder::basic::kvp;

        //Read a few Dog records

        {
            document filter;
            filter.append(kvp("animal_type", "Dog"));

            auto dogs = shelter.read(filter.view(), 3);
            std::cout << "[TEST] Read(Dog) returned: " << dogs.size() << " docs\n";

            for (auto& d : dogs) {
                std::cout << "  " << bsoncxx::to_json(d.view()) << "\n";
            }
        }

        //Insert a test record

        const std::string test_id = "A999999";

        {
            document doc;
            doc.append(kvp("animal_id", test_id));
            doc.append(kvp("animal_type", "Dog"));
            doc.append(kvp("breed", "Test Breed"));
            doc.append(kvp("age_upon_outcome_in_weeks", 10));
            doc.append(kvp("sex_upon_outcome", "Neutered Male"));
            doc.append(kvp("outcome_type", "Adoption"));

            // Clean up
            document cleanup;
            cleanup.append(kvp("animal_id", test_id));
            shelter.remove(cleanup.view());

            bool created = shelter.create(doc.view());
            std::cout << "[TEST] Create(test doc): " << (created ? "true" : "false") << "\n";

            if (!created) {
                std::cerr << "[FAIL] Insert failed.\n";
                return 2;
            }
        }

        //Read the test record back
        {
            document filter;
            filter.append(kvp("animal_id", test_id));

            auto found = shelter.read(filter.view(), 10);
            std::cout << "[TEST] Read(test doc) returned: " << found.size() << " docs\n";

            if (found.empty()) {
                std::cerr << "[FAIL] Inserted document not found.\n";
                return 3;
            }

            std::cout << "  " << bsoncxx::to_json(found.front().view()) << "\n";
        }


        //Delete the test record
        {
            document filter;
            filter.append(kvp("animal_id", test_id));

            auto deleted = shelter.remove(filter.view());
            std::cout << "[TEST] Remove(test doc) deleted: " << deleted << "\n";

            if (deleted <= 0) {
                std::cerr << "[FAIL] Delete did not remove anything.\n";
                return 4;
            }
        }

        //Make sure it is actually gone
        {
            document filter;
            filter.append(kvp("animal_id", test_id));

            auto found = shelter.read(filter.view(), 10);
            std::cout << "[TEST] Verify delete, remaining: " << found.size() << "\n";

            if (!found.empty()) {
                std::cerr << "[FAIL] Document still exists after delete.\n";
                return 5;
            }
        }

        std::cout << "\n[SUCCESS] All CRUD tests passed.\n";
        return 0;
    }
    catch (const mongocxx::exception& ex) {
        std::cerr << "[MONGO ERROR] " << ex.what() << "\n";
        std::cerr << "Make sure MongoDB is running and AAC_MONGO_URI is correct.\n";
        return 10;
    }
    catch (const std::exception& ex) {
        std::cerr << "[ERROR] " << ex.what() << "\n";
        return 11;
    }
}
