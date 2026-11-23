// A self-balancing AVL tree with a virtual comparison hook.
//
// EngineerTree orders last names; IntegerTree orders integers. The insertion
// order for the engineer names is deliberately alphabetical so the AVL
// rotations are exercised even by the best-case BST input order.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TreeNode {
    const void* data;
    TreeNode* left;
    TreeNode* right;
    int height;
};

class BalancedTree {
public:
    BalancedTree() {
        root_ = 0;
        count_ = 0;
    }

    virtual ~BalancedTree() {
        clear(root_);
    }

    void add(const void* data) {
        root_ = insert(root_, data);
        count_ = count_ + 1;
    }

    void print() const {
        printNode(root_);
    }

    int size() const {
        return count_;
    }

protected:
    virtual int compare(const void* left, const void* right) const = 0;
    virtual void printValue(const void* data) const = 0;

private:
    static int heightOf(TreeNode* node) {
        return node == 0 ? 0 : node->height;
    }

    static int maximum(int left, int right) {
        return left > right ? left : right;
    }

    static TreeNode* rotateLeft(TreeNode* node) {
        TreeNode* pivot = node->right;
        node->right = pivot->left;
        pivot->left = node;
        node->height = maximum(heightOf(node->left), heightOf(node->right)) + 1;
        pivot->height = maximum(heightOf(pivot->left), heightOf(pivot->right)) + 1;
        return pivot;
    }

    static TreeNode* rotateRight(TreeNode* node) {
        TreeNode* pivot = node->left;
        node->left = pivot->right;
        pivot->right = node;
        node->height = maximum(heightOf(node->left), heightOf(node->right)) + 1;
        pivot->height = maximum(heightOf(pivot->left), heightOf(pivot->right)) + 1;
        return pivot;
    }

    static TreeNode* balance(TreeNode* node) {
        node->height = maximum(heightOf(node->left), heightOf(node->right)) + 1;
        const int factor = heightOf(node->left) - heightOf(node->right);
        if (factor > 1) {
            if (heightOf(node->left->left) < heightOf(node->left->right)) {
                node->left = rotateLeft(node->left);
            }
            return rotateRight(node);
        }
        if (factor < -1) {
            if (heightOf(node->right->right) < heightOf(node->right->left)) {
                node->right = rotateRight(node->right);
            }
            return rotateLeft(node);
        }
        return node;
    }

    TreeNode* insert(TreeNode* node, const void* data) {
        if (node == 0) {
            TreeNode* created = new TreeNode;
            created->data = data;
            created->left = 0;
            created->right = 0;
            created->height = 1;
            return created;
        }
        if (compare(data, node->data) < 0) {
            node->left = insert(node->left, data);
        } else {
            node->right = insert(node->right, data);
        }
        return balance(node);
    }

    void printNode(TreeNode* node) const {
        if (node == 0) {
            return;
        }
        printNode(node->left);
        printValue(node->data);
        printNode(node->right);
    }

    static void clear(TreeNode* node) {
        if (node == 0) {
            return;
        }
        clear(node->left);
        clear(node->right);
        delete node;
    }

    TreeNode* root_;
    int count_;
};

class EngineerTree : public BalancedTree {
protected:
    int compare(const void* left, const void* right) const {
        return strcmp((const char*)left, (const char*)right);
    }

    void printValue(const void* data) const {
        printf("  %s\n", (const char*)data);
    }
};

class IntegerTree : public BalancedTree {
protected:
    int compare(const void* left, const void* right) const {
        const int a = *(const int*)left;
        const int b = *(const int*)right;
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
        return 0;
    }

    void printValue(const void* data) const {
        printf("  %d\n", *(const int*)data);
    }
};

int main() {
    const char* engineers[30] = {
        "Archimedes", "Babbage", "Bardeen", "Bell", "Berners-Lee", "Brattain",
        "Brunel", "Cray", "Edison", "Galilei", "Grove", "Hamilton", "Hopper",
        "Kilby", "Lamarr", "Lovelace", "Marconi", "Moore", "Newton", "Noyce",
        "Shockley", "Smeaton", "Stephenson", "Telford", "Tesla", "Torvalds",
        "Trevithick", "Vinci", "Watt", "Wozniak"
    };

    EngineerTree engineersTree;
    for (int index = 0; index < 30; ++index) {
        engineersTree.add(engineers[index]);
    }

    printf("Engineers in sorted order (%d):\n", engineersTree.size());
    engineersTree.print();

    IntegerTree integerTree;
    int values[20];
    for (int index = 0; index < 20; ++index) {
        int value = sys_random() % 1000;
        if (value < 0) {
            value = -value;
        }
        values[index] = value + 1;
        integerTree.add(&values[index]);
    }

    printf("Random integers in sorted order (%d):\n", integerTree.size());
    integerTree.print();
    return 0;
}
