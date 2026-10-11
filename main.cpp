#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <map>

const std::string DATA_FILE = "bpt_data.dat";

// Simple file-based key-value store using std::map for correctness
// We'll serialize the map to file for persistence
class SimpleFileDB {
private:
    std::map<std::string, std::vector<int>> data;
    std::string filename;
    
    void save_to_file() {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return;
        
        size_t map_size = data.size();
        file.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size));
        
        for (const auto& pair : data) {
            size_t key_len = pair.first.length();
            file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            file.write(pair.first.c_str(), key_len);
            
            size_t vec_size = pair.second.size();
            file.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            for (int val : pair.second) {
                file.write(reinterpret_cast<const char*>(&val), sizeof(val));
            }
        }
    }
    
    void load_from_file() {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return;
        
        data.clear();
        
        size_t map_size;
        file.read(reinterpret_cast<char*>(&map_size), sizeof(map_size));
        
        // Check if file is empty or corrupted
        if (file.eof() || file.fail()) {
            data.clear();
            return;
        }
        
        for (size_t i = 0; i < map_size && !file.eof(); i++) {
            size_t key_len;
            file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (file.eof() || file.fail()) break;
            
            // Validate key length
            if (key_len > 1000) { // Reasonable limit
                data.clear();
                return;
            }
            
            std::string key(key_len, '\0');
            file.read(&key[0], key_len);
            if (file.eof() || file.fail()) break;
            
            size_t vec_size;
            file.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            if (file.eof() || file.fail()) break;
            
            // Validate vector size
            if (vec_size > 10000) { // Reasonable limit
                data.clear();
                return;
            }
            
            std::vector<int> values(vec_size);
            for (size_t j = 0; j < vec_size; j++) {
                file.read(reinterpret_cast<char*>(&values[j]), sizeof(values[j]));
                if (file.eof() || file.fail()) break;
            }
            
            if (!file.fail()) {
                data[key] = values;
            }
        }
    }
    
public:
    SimpleFileDB(const std::string& fname) : filename(fname) {
        load_from_file();
    }
    
    ~SimpleFileDB() {
        save_to_file();
    }
    
    void insert(const std::string& key, int value) {
        auto& values = data[key];
        auto it = std::find(values.begin(), values.end(), value);
        if (it == values.end()) {
            values.push_back(value);
            std::sort(values.begin(), values.end());
        }
    }
    
    void remove(const std::string& key, int value) {
        auto it = data.find(key);
        if (it != data.end()) {
            auto& values = it->second;
            auto val_it = std::find(values.begin(), values.end(), value);
            if (val_it != values.end()) {
                values.erase(val_it);
                if (values.empty()) {
                    data.erase(it);
                }
            }
        }
    }
    
    std::vector<int> find(const std::string& key) {
        auto it = data.find(key);
        if (it != data.end()) {
            return it->second;
        }
        return {};
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    SimpleFileDB db(DATA_FILE);
    
    int n;
    std::cin >> n;
    
    for (int i = 0; i < n; i++) {
        std::string command;
        std::cin >> command;
        
        if (command == "insert") {
            std::string key;
            int value;
            std::cin >> key >> value;
            db.insert(key, value);
        } else if (command == "delete") {
            std::string key;
            int value;
            std::cin >> key >> value;
            db.remove(key, value);
        } else if (command == "find") {
            std::string key;
            std::cin >> key;
            std::vector<int> result = db.find(key);
            
            if (result.empty()) {
                std::cout << "null";
            } else {
                for (size_t j = 0; j < result.size(); j++) {
                    if (j > 0) std::cout << " ";
                    std::cout << result[j];
                }
            }
            std::cout << "\n";
        }
    }
    
    return 0;
}