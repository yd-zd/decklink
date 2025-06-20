#pragma once

#include <unordered_map>
#include <vector>

namespace nos::decklink
{

template <typename K>
struct TrieNode {
	std::unordered_map<K, TrieNode<K>*> Children{};
};

template <typename K>
class Trie {
public:
	Trie() : Root(new TrieNode<K>()) {}
	~Trie() { DeleteNode(Root); }

	void Insert(const std::vector<K>& key)
	{
		TrieNode<K>* currentNode = Root;
		for (const K& part : key) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				currentNode->Children[part] = new TrieNode<K>();
			}
			currentNode = currentNode->Children[part];
		}
	}

	bool Contains(const std::vector<K>& key)
	{
		TrieNode<K>* currentNode = Root;
		for (const K& part : key) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				return false;
			}
			currentNode = currentNode->Children[part];
		}
		return true;
	}

	void Search(const std::vector<K>& prefix, std::vector<std::vector<K>>& results)
	{
		TrieNode<K>* currentNode = Root;
		for (const K& part : prefix) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				return; // Prefix not found
			}
			currentNode = currentNode->Children[part];
		}

		std::vector<K> currentPath = prefix;
		DFS(currentNode, currentPath, results);
	}

private:
	void DeleteNode(TrieNode<K>* node)
	{
		for (auto& child : node->Children) {
			DeleteNode(child.second);
		}
		delete node;
	}

	void DFS(TrieNode<K>* node, std::vector<K>& path, std::vector<std::vector<K>>& results)
	{
		if (node->Children.empty()) {
			results.push_back(path);
		}

		for (auto& child : node->Children) {
			path.push_back(child.first);
			DFS(child.second, path, results);
			path.pop_back();
		}
	}

	TrieNode<K>* Root;
};

}