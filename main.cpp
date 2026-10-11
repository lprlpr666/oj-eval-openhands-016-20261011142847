#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>

const std::string DATA_FILE = "bpt_data.dat";
const int MAX_KEY_LENGTH = 64;
const int ORDER = 4; // Small order for simplicity
const int LEAF_CAPACITY = 3; // Small capacity for simplicity

// B+ Tree node types
enum NodeType {
    INTERNAL,
    LEAF
};

struct KeyValue {
    char key[MAX_KEY_LENGTH];
    int value;
    
    KeyValue() {
        memset(key, 0, MAX_KEY_LENGTH);
        value = 0;
    }
    
    KeyValue(const std::string& k, int v) : value(v) {
        strncpy(key, k.c_str(), MAX_KEY_LENGTH - 1);
        key[MAX_KEY_LENGTH - 1] = '\0';
    }
    
    bool operator<(const KeyValue& other) const {
        return strcmp(key, other.key) < 0;
    }
    
    bool operator==(const KeyValue& other) const {
        return strcmp(key, other.key) == 0 && value == other.value;
    }
};

struct BPTNode {
    NodeType type;
    int count;
    std::vector<KeyValue> keys;
    std::vector<int> children; // For internal nodes
    int next_leaf; // For leaf nodes
    
    BPTNode(NodeType t) : type(t), count(0), next_leaf(-1) {
        if (type == INTERNAL) {
            keys.resize(ORDER - 1);
            children.resize(ORDER);
        } else {
            keys.resize(LEAF_CAPACITY);
        }
    }
};

class BPlusTree {
private:
    std::fstream file;
    int root_offset;
    int next_offset;
    
    void write_node(int offset, const BPTNode& node) {
        file.seekp(offset);
        
        // Write node type
        int type_int = (node.type == INTERNAL) ? 0 : 1;
        file.write(reinterpret_cast<const char*>(&type_int), sizeof(type_int));
        
        // Write count
        file.write(reinterpret_cast<const char*>(&node.count), sizeof(node.count));
        
        // Write keys
        for (int i = 0; i < node.count; i++) {
            file.write(node.keys[i].key, MAX_KEY_LENGTH);
            file.write(reinterpret_cast<const char*>(&node.keys[i].value), sizeof(node.keys[i].value));
        }
        
        // Write children or next_leaf
        if (node.type == INTERNAL) {
            for (int i = 0; i <= node.count; i++) {
                file.write(reinterpret_cast<const char*>(&node.children[i]), sizeof(node.children[i]));
            }
        } else {
            file.write(reinterpret_cast<const char*>(&node.next_leaf), sizeof(node.next_leaf));
        }
    }
    
    BPTNode read_node(int offset) {
        file.seekg(offset);
        
        // Read node type
        int type_int;
        file.read(reinterpret_cast<char*>(&type_int), sizeof(type_int));
        NodeType type = (type_int == 0) ? INTERNAL : LEAF;
        
        BPTNode node(type);
        
        // Read count
        file.read(reinterpret_cast<char*>(&node.count), sizeof(node.count));
        
        // Read keys
        for (int i = 0; i < node.count; i++) {
            file.read(node.keys[i].key, MAX_KEY_LENGTH);
            file.read(reinterpret_cast<char*>(&node.keys[i].value), sizeof(node.keys[i].value));
        }
        
        // Read children or next_leaf
        if (node.type == INTERNAL) {
            for (int i = 0; i <= node.count; i++) {
                file.read(reinterpret_cast<char*>(&node.children[i]), sizeof(node.children[i]));
            }
        } else {
            file.read(reinterpret_cast<char*>(&node.next_leaf), sizeof(node.next_leaf));
        }
        
        return node;
    }
    
    int allocate_node() {
        int offset = next_offset;
        next_offset += 4096; // Fixed size per node
        return offset;
    }
    
    int find_leaf(const std::string& key) {
        if (root_offset == -1) return -1;
        
        int current = root_offset;
        while (true) {
            BPTNode node = read_node(current);
            
            if (node.type == LEAF) {
                return current;
            }
            
            // Find child to follow
            int child_idx = 0;
            while (child_idx < node.count && strcmp(key.c_str(), node.keys[child_idx].key) >= 0) {
                child_idx++;
            }
            
            current = node.children[child_idx];
        }
    }
    
    void split_leaf(int leaf_offset, const KeyValue& new_kv) {
        BPTNode leaf = read_node(leaf_offset);
        
        // Create new leaf
        BPTNode new_leaf(LEAF);
        new_leaf.next_leaf = leaf.next_leaf;
        
        // Temporary array for splitting
        std::vector<KeyValue> all_keys;
        for (int i = 0; i < leaf.count; i++) {
            all_keys.push_back(leaf.keys[i]);
        }
        all_keys.push_back(new_kv);
        std::sort(all_keys.begin(), all_keys.end());
        
        // Split
        int split_pos = (LEAF_CAPACITY + 1) / 2;
        for (int i = 0; i < split_pos; i++) {
            leaf.keys[i] = all_keys[i];
        }
        leaf.count = split_pos;
        
        for (size_t i = split_pos; i < all_keys.size(); i++) {
            new_leaf.keys[i - split_pos] = all_keys[i];
        }
        new_leaf.count = all_keys.size() - split_pos;
        
        // Update links
        leaf.next_leaf = allocate_node();
        
        // Write nodes
        write_node(leaf_offset, leaf);
        write_node(leaf.next_leaf, new_leaf);
        
        // Insert into parent (simplified - just create new root if needed)
        if (leaf_offset == root_offset) {
            BPTNode new_root(INTERNAL);
            new_root.keys[0] = new_leaf.keys[0];
            new_root.children[0] = leaf_offset;
            new_root.children[1] = leaf.next_leaf;
            new_root.count = 1;
            
            root_offset = allocate_node();
            write_node(root_offset, new_root);
        }
    }
    
    void insert_into_leaf(const std::string& key, int value) {
        if (root_offset == -1) {
            // Create root leaf
            BPTNode root(LEAF);
            root.keys[0] = KeyValue(key, value);
            root.count = 1;
            
            root_offset = allocate_node();
            write_node(root_offset, root);
            return;
        }
        
        int leaf_offset = find_leaf(key);
        BPTNode leaf = read_node(leaf_offset);
        
        // Check if duplicate
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.keys[i].key, key.c_str()) == 0 && leaf.keys[i].value == value) {
                return; // Duplicate
            }
        }
        
        if (leaf.count < LEAF_CAPACITY) {
            // Insert directly
            int pos = 0;
            while (pos < leaf.count && strcmp(key.c_str(), leaf.keys[pos].key) > 0) {
                pos++;
            }
            while (pos < leaf.count && strcmp(key.c_str(), leaf.keys[pos].key) == 0 && value > leaf.keys[pos].value) {
                pos++;
            }
            
            for (int i = leaf.count; i > pos; i--) {
                leaf.keys[i] = leaf.keys[i - 1];
            }
            
            leaf.keys[pos] = KeyValue(key, value);
            leaf.count++;
            
            write_node(leaf_offset, leaf);
        } else {
            // Split leaf
            split_leaf(leaf_offset, KeyValue(key, value));
        }
    }
    
public:
    BPlusTree() : root_offset(-1), next_offset(0) {
        std::ifstream test_file(DATA_FILE);
        if (test_file.good()) {
            test_file.close();
            file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);
            
            // Read header (simplified - just read root offset)
            file.seekg(0);
            file.read(reinterpret_cast<char*>(&root_offset), sizeof(root_offset));
            file.read(reinterpret_cast<char*>(&next_offset), sizeof(next_offset));
        } else {
            file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
            
            // Write header
            root_offset = -1;
            next_offset = 16; // Start after header
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&root_offset), sizeof(root_offset));
            file.write(reinterpret_cast<const char*>(&next_offset), sizeof(next_offset));
        }
    }
    
    ~BPlusTree() {
        // Write header
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&root_offset), sizeof(root_offset));
        file.write(reinterpret_cast<const char*>(&next_offset), sizeof(next_offset));
        file.close();
    }
    
    void insert(const std::string& key, int value) {
        insert_into_leaf(key, value);
    }
    
    void remove(const std::string& key, int value) {
        if (root_offset == -1) return;
        
        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) return;
        
        BPTNode leaf = read_node(leaf_offset);
        
        // Find and remove
        bool found = false;
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.keys[i].key, key.c_str()) == 0 && leaf.keys[i].value == value) {
                found = true;
                for (int j = i; j < leaf.count - 1; j++) {
                    leaf.keys[j] = leaf.keys[j + 1];
                }
                leaf.count--;
                break;
            }
        }
        
        if (found) {
            write_node(leaf_offset, leaf);
        }
    }
    
    std::vector<int> find(const std::string& key) {
        std::vector<int> result;
        
        if (root_offset == -1) return result;
        
        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) return result;
        
        BPTNode leaf = read_node(leaf_offset);
        
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.keys[i].key, key.c_str()) == 0) {
                result.push_back(leaf.keys[i].value);
            }
        }
        
        // Sort values in ascending order
        std::sort(result.begin(), result.end());
        
        return result;
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    BPlusTree bpt;
    
    int n;
    std::cin >> n;
    
    for (int i = 0; i < n; i++) {
        std::string command;
        std::cin >> command;
        
        if (command == "insert") {
            std::string key;
            int value;
            std::cin >> key >> value;
            bpt.insert(key, value);
        } else if (command == "delete") {
            std::string key;
            int value;
            std::cin >> key >> value;
            bpt.remove(key, value);
        } else if (command == "find") {
            std::string key;
            std::cin >> key;
            std::vector<int> result = bpt.find(key);
            
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