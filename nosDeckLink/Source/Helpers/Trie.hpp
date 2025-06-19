#pragma once

#include <unordered_map>
#include <vector>

namespace nos::decklink
{

template <typename K, typename V>
struct TrieNode {
	V Data{};
	std::unordered_map<K, TrieNode<K, V>*> Children{};
};

template <typename K, typename V>
class Trie {
public:
	Trie() : Root(new TrieNode<K, V>()) {}
	~Trie() { DeleteNode(Root); }

	void Insert(const std::vector<K>& key, const V& value) {
		TrieNode<K, V>* currentNode = Root;
		for (const K& part : key) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				currentNode->Children[part] = new TrieNode<K, V>();
			}
			currentNode = currentNode->Children[part];
		}
		currentNode->Data = value;
	}

	V* Find(const std::vector<K>& key) {
		TrieNode<K, V>* currentNode = Root;
		for (const K& part : key) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				return nullptr;
			}
			currentNode = currentNode->Children[part];
		}
		return &currentNode->Data;
	}

	void Search(const std::vector<K>& prefix, std::vector<V*>& results) {
		TrieNode<K, V>* currentNode = Root;
		for (const K& part : prefix) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				return;
			}
			currentNode = currentNode->Children[part];
		}
		results.push_back(&currentNode->Data);
		for (const auto& child : currentNode->Children) {
			std::vector<K> newPrefix = prefix;
			newPrefix.push_back(child.first);
			Search(newPrefix, results);
		}
	}

private:
	void DeleteNode(TrieNode<K, V>* node) {
		for (auto& child : node->Children) {
			DeleteNode(child.second);
		}
		delete node;
	}

	TrieNode<K, V>* Root;
};

}