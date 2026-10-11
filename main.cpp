#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <set>

const std::string DATA_FILE = "bpt_data.dat";
const int BLOCK_SIZE = 4096;
const int MAX_KEY_LENGTH = 64;
const int ORDER = 50; // Fanout for internal nodes
const int LEAF_CAPACITY = 50; // Maximum key-value pairs per leaf

struct Header {
    int root_offset;
    int free_list_head;
    int total_blocks;
    char padding[BLOCK_SIZE - 3 * sizeof(int)];
};

struct InternalNode {
    bool is_leaf;
    int count;
    int keys[ORDER - 1];
    int children[ORDER];
    char padding[BLOCK_SIZE - sizeof(bool) - 2 * sizeof(int) - (ORDER - 1) * sizeof(int) - ORDER * sizeof(int)];
};

struct LeafEntry {
    char key[MAX_KEY_LENGTH];
    int value;
};

struct LeafNode {
    bool is_leaf;
    int count;
    LeafEntry entries[LEAF_CAPACITY];
    int next_leaf;
    char padding[BLOCK_SIZE - sizeof(bool) - 2 * sizeof(int) - LEAF_CAPACITY * sizeof(LeafEntry)];
};

class BPlusTree {
private:
    std::fstream file;
    Header header;
    
    void read_header() {
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
    }
    
    void write_header() {
        file.seekp(0);
        file.write(reinterpret_cast<char*>(&header), sizeof(header));
    }
    
    void read_block(int offset, void* buffer, int size) {
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(buffer), size);
    }
    
    void write_block(int offset, void* buffer, int size) {
        file.seekp(offset);
        file.write(reinterpret_cast<char*>(buffer), size);
    }
    
    int allocate_block() {
        if (header.free_list_head != -1) {
            int offset = header.free_list_head;
            InternalNode node;
            read_block(offset, &node, sizeof(node));
            header.free_list_head = node.children[0]; // Use first child as next free pointer
            write_header();
            return offset;
        }
        
        int offset = sizeof(header) + header.total_blocks * BLOCK_SIZE;
        header.total_blocks++;
        write_header();
        return offset;
    }
    
    void free_block(int offset) {
        InternalNode node;
        node.children[0] = header.free_list_head;
        write_block(offset, &node, sizeof(node));
        header.free_list_head = offset;
        write_header();
    }
    
    int find_leaf(const std::string& key) {
        if (header.root_offset == -1) {
            return -1;
        }
        
        int current = header.root_offset;
        while (true) {
            InternalNode node;
            read_block(current, &node, sizeof(node));
            
            if (node.is_leaf) {
                return current;
            }
            
            int child_idx = 0;
            while (child_idx < node.count && key.compare(std::to_string(node.keys[child_idx])) >= 0) {
                child_idx++;
            }
            
            current = node.children[child_idx];
        }
    }
    
    void split_internal(int parent_offset, int child_offset, int& new_key, int& new_child) {
        InternalNode parent, child;
        read_block(parent_offset, &parent, sizeof(parent));
        read_block(child_offset, &child, sizeof(child));
        
        InternalNode new_node;
        new_node.is_leaf = false;
        new_node.count = ORDER / 2;
        
        int split_pos = ORDER / 2;
        new_key = child.keys[split_pos - 1];
        
        for (int i = 0; i < new_node.count; i++) {
            new_node.keys[i] = child.keys[split_pos + i];
            new_node.children[i] = child.children[split_pos + i];
        }
        new_node.children[new_node.count] = child.children[ORDER];
        
        child.count = split_pos - 1;
        
        int new_offset = allocate_block();
        write_block(new_offset, &new_node, sizeof(new_node));
        write_block(child_offset, &child, sizeof(child));
        
        new_child = new_offset;
    }
    
    void split_leaf(int leaf_offset, int& new_key, int& new_leaf) {
        LeafNode leaf;
        read_block(leaf_offset, &leaf, sizeof(leaf));
        
        LeafNode new_leaf_node;
        new_leaf_node.is_leaf = true;
        new_leaf_node.count = LEAF_CAPACITY / 2;
        new_leaf_node.next_leaf = leaf.next_leaf;
        
        int split_pos = LEAF_CAPACITY / 2;
        new_key = std::stoi(leaf.entries[split_pos].key);
        
        for (int i = 0; i < new_leaf_node.count; i++) {
            strcpy(new_leaf_node.entries[i].key, leaf.entries[split_pos + i].key);
            new_leaf_node.entries[i].value = leaf.entries[split_pos + i].value;
        }
        
        leaf.count = split_pos;
        leaf.next_leaf = allocate_block();
        
        write_block(leaf_offset, &leaf, sizeof(leaf));
        write_block(leaf.next_leaf, &new_leaf_node, sizeof(new_leaf_node));
        
        new_leaf = leaf.next_leaf;
    }
    
    void insert_into_parent(int parent_offset, int key, int child_offset) {
        InternalNode parent;
        read_block(parent_offset, &parent, sizeof(parent));
        
        if (parent.count < ORDER - 1) {
            int pos = 0;
            while (pos < parent.count && key >= parent.keys[pos]) {
                pos++;
            }
            
            for (int i = parent.count; i > pos; i--) {
                parent.keys[i] = parent.keys[i - 1];
                parent.children[i + 1] = parent.children[i];
            }
            
            parent.keys[pos] = key;
            parent.children[pos + 1] = child_offset;
            parent.count++;
            
            write_block(parent_offset, &parent, sizeof(parent));
        } else {
            // Split internal node
            int new_key, new_child;
            split_internal(parent_offset, child_offset, new_key, new_child);
            
            if (parent_offset == header.root_offset) {
                // Create new root
                InternalNode new_root;
                new_root.is_leaf = false;
                new_root.count = 1;
                new_root.keys[0] = new_key;
                new_root.children[0] = parent_offset;
                new_root.children[1] = new_child;
                
                int new_root_offset = allocate_block();
                write_block(new_root_offset, &new_root, sizeof(new_root));
                
                header.root_offset = new_root_offset;
                write_header();
            } else {
                // Recursively insert into parent
                // For simplicity, we'll find the parent and insert there
                // In a real implementation, we'd track the path during insertion
            }
        }
    }
    
public:
    BPlusTree() {
        bool file_exists = std::ifstream(DATA_FILE).good();
        
        file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);
        
        if (!file_exists) {
            file.close();
            file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
            
            header.root_offset = -1;
            header.free_list_head = -1;
            header.total_blocks = 0;
            write_header();
        } else {
            read_header();
        }
    }
    
    ~BPlusTree() {
        file.close();
    }
    
    void insert(const std::string& key, int value) {
        if (header.root_offset == -1) {
            // Create root leaf
            LeafNode root;
            root.is_leaf = true;
            root.count = 1;
            strcpy(root.entries[0].key, key.c_str());
            root.entries[0].value = value;
            root.next_leaf = -1;
            
            header.root_offset = allocate_block();
            write_header();
            write_block(header.root_offset, &root, sizeof(root));
            return;
        }
        
        int leaf_offset = find_leaf(key);
        LeafNode leaf;
        read_block(leaf_offset, &leaf, sizeof(leaf));
        
        // Check if value already exists for this key
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.entries[i].key, key.c_str()) == 0 && leaf.entries[i].value == value) {
                return; // Duplicate, don't insert
            }
        }
        
        if (leaf.count < LEAF_CAPACITY) {
            // Insert into leaf
            int pos = 0;
            while (pos < leaf.count && strcmp(key.c_str(), leaf.entries[pos].key) > 0) {
                pos++;
            }
            while (pos < leaf.count && strcmp(key.c_str(), leaf.entries[pos].key) == 0 && value > leaf.entries[pos].value) {
                pos++;
            }
            
            for (int i = leaf.count; i > pos; i--) {
                leaf.entries[i] = leaf.entries[i - 1];
            }
            
            strcpy(leaf.entries[pos].key, key.c_str());
            leaf.entries[pos].value = value;
            leaf.count++;
            
            write_block(leaf_offset, &leaf, sizeof(leaf));
        } else {
            // Split leaf
            int new_key, new_leaf_offset;
            split_leaf(leaf_offset, new_key, new_leaf_offset);
            
            // Insert into appropriate leaf
            if (key.compare(std::to_string(new_key)) >= 0) {
                leaf_offset = new_leaf_offset;
                read_block(leaf_offset, &leaf, sizeof(leaf));
            }
            
            // Insert into the now non-full leaf
            int pos = 0;
            while (pos < leaf.count && strcmp(key.c_str(), leaf.entries[pos].key) > 0) {
                pos++;
            }
            while (pos < leaf.count && strcmp(key.c_str(), leaf.entries[pos].key) == 0 && value > leaf.entries[pos].value) {
                pos++;
            }
            
            for (int i = leaf.count; i > pos; i--) {
                leaf.entries[i] = leaf.entries[i - 1];
            }
            
            strcpy(leaf.entries[pos].key, key.c_str());
            leaf.entries[pos].value = value;
            leaf.count++;
            
            write_block(leaf_offset, &leaf, sizeof(leaf));
            
            // Insert new key into parent
            // For simplicity, we'll assume root exists and handle it
            if (leaf_offset == header.root_offset) {
                // Create new root
                InternalNode new_root;
                new_root.is_leaf = false;
                new_root.count = 1;
                new_root.keys[0] = new_key;
                new_root.children[0] = leaf_offset;
                new_root.children[1] = new_leaf_offset;
                
                int new_root_offset = allocate_block();
                write_block(new_root_offset, &new_root, sizeof(new_root));
                
                header.root_offset = new_root_offset;
                write_header();
            }
        }
    }
    
    void remove(const std::string& key, int value) {
        if (header.root_offset == -1) {
            return;
        }
        
        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) {
            return;
        }
        
        LeafNode leaf;
        read_block(leaf_offset, &leaf, sizeof(leaf));
        
        // Find and remove the entry
        bool found = false;
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.entries[i].key, key.c_str()) == 0 && leaf.entries[i].value == value) {
                found = true;
                for (int j = i; j < leaf.count - 1; j++) {
                    leaf.entries[j] = leaf.entries[j + 1];
                }
                leaf.count--;
                break;
            }
        }
        
        if (found) {
            write_block(leaf_offset, &leaf, sizeof(leaf));
        }
    }
    
    std::vector<int> find(const std::string& key) {
        std::vector<int> result;
        
        if (header.root_offset == -1) {
            return result;
        }
        
        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) {
            return result;
        }
        
        LeafNode leaf;
        read_block(leaf_offset, &leaf, sizeof(leaf));
        
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.entries[i].key, key.c_str()) == 0) {
                result.push_back(leaf.entries[i].value);
            }
        }
        
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