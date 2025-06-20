#pragma once

#include <unordered_map>
#include <vector>

namespace nos::decklink
{

template <typename K>
struct PrefixTreeNode {
	std::unordered_map<K, PrefixTreeNode<K>*> Children{};
};

template <typename K>
class PrefixTree {
public:
	PrefixTree() : Root(new PrefixTreeNode<K>()) {}
	~PrefixTree() { DeleteNode(Root); }

	void Insert(const std::vector<K>& key)
	{
		PrefixTreeNode<K>* currentNode = Root;
		for (const K& part : key) {
			if (currentNode->Children.find(part) == currentNode->Children.end()) {
				currentNode->Children[part] = new PrefixTreeNode<K>();
			}
			currentNode = currentNode->Children[part];
		}
	}

	bool Contains(const std::vector<K>& key)
	{
		PrefixTreeNode<K>* currentNode = Root;
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
		PrefixTreeNode<K>* currentNode = Root;
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
	void DeleteNode(PrefixTreeNode<K>* node)
	{
		for (auto& child : node->Children) {
			DeleteNode(child.second);
		}
		delete node;
	}

	void DFS(PrefixTreeNode<K>* node, std::vector<K>& path, std::vector<std::vector<K>>& results)
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

	PrefixTreeNode<K>* Root;
};

}