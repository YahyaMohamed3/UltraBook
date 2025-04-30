#include<iostream>
#include<string>
#include<chrono>
#include<ctime>
#include<vector>
#include<sstream>

struct Transaction {
    std::string sender;
    std::string receiver;
    double amount;
    std::chrono::system_clock::time_point timestamp;
    std::string transactionID;

    static int counter;

    Transaction(const std::string &from, const std::string &to, double amt)
        : sender(from), receiver(to), amount(amt), timestamp(std::chrono::system_clock::now()) //  Timestamp is captured at transaction creation.
    {
        transactionID = sender + receiver + std::to_string(amount) + std::to_string(++counter); //  Unique ID to help prevent duplicate processing or collisions.
    }

    void display() const {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp); // Convert to time_t for human-readable output.
        std::cout << "Transaction ID: " << transactionID << std::endl;
        std::cout << "Sender: " << sender << std::endl;
        std::cout << "Receiver: " << receiver << std::endl;
        std::cout << "Amount: " << amount << std::endl;
        std::cout << "Timestamp: " << std::ctime(&time); //  std::ctime is NOT thread-safe. Safe here since it's for single-threaded display only.
    }
};
int Transaction::counter = 0;

// Dummy SHA256 class so hash can be filled later
class SHA256 {
public:
    static std::string hash(const std::string& input) {
        return "DUMMY_HASH_" + input.substr(0, 10); //  Replace with real hash function later
    }
};

class Block {
    int index;
    std::string previousHash;
    std::string currentHash;
    std::vector<Transaction> transactions; //  Transaction copies here could be costly in high-frequency systems — consider using pointers or move semantics if needed.
    std::chrono::system_clock::time_point timestamp;
    int nonce;

public:
    Block(int idx, const std::string &prehash, const std::vector<Transaction> &tsx)
        : index(idx), previousHash(prehash), transactions(tsx), nonce(0), timestamp(std::chrono::system_clock::now()) {
        currentHash = calculateHash(); // ⏱ Critical for block integrity: must happen after all block contents are set.
    }
    int getIndex() const { return index; }
    std::string getHash() const { return currentHash; }
    std::string getPreviousHash() const { return previousHash; }


    std::string calculateHash() const {
        std::stringstream ss;
        ss << index << previousHash << nonce << std::chrono::system_clock::to_time_t(timestamp);
        for (const auto &tx : transactions) {
            ss << tx.transactionID; //  We're only hashing TX IDs — in real systems, hash the whole TX content.
        }
        return SHA256::hash(ss.str());
    }

    void printBlock() const {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        std::cout << "Block Index: " << index << std::endl;
        std::cout << "Previous Hash: " << previousHash << std::endl;
        std::cout << "Current Hash: " << currentHash << std::endl;
        std::cout << "Timestamp: " << std::ctime(&time);
        std::cout << "Transactions: " << std::endl;
        for (const auto &tx : transactions) {
            tx.display();
            std::cout << "------------------------" << std::endl;
        }
    }
};

class Blockchain {
    private:
        std::vector<Block> chain;
    
        // Creates the first block in the blockchain
        Block createGenesisBlock() {
            std::vector<Transaction> genesisTxs; // empty or predefined
            return Block(0, "0", genesisTxs);     // index 0, prevHash = "0"
        }
    
    public:
        // Constructor
        Blockchain() {
            chain.push_back(createGenesisBlock());
        }
    
        // Add new block with given transactions
        void addBlock(const std::vector<Transaction>& transactions) {
            Block latestBlock = getLatestBlock();
            int newIndex = latestBlock.getIndex() + 1;
            std::string prevHash = latestBlock.getHash();
            Block newBlock(newIndex, prevHash, transactions);
            chain.push_back(newBlock);
        }
    
        // Returns the latest block
        Block getLatestBlock() const {
            return chain.back();
        }
    
        // Validates blockchain's integrity
        bool isChainValid() const {
            //iterate through each block starting from the second one(index 1 )
            for (size_t i = 1; i < chain.size(); ++i) {
                 // Get the current block and the previous block
                const Block& current = chain[i];
                const Block& previous = chain[i - 1];
    
                if (current.getHash() != current.calculateHash())
                    return false;
    
                if (current.getPreviousHash() != previous.getHash())
                    return false;
            }
            return true;
        }
    
        // Print the entire chain
        void printChain() const {
            for (const Block& block : chain) {
                block.printBlock();
                std::cout << "-----------------------\n";
            }
        }
    };


// Wallet class to manage transactions and balance
class Wallet {
    std::string ownerName;
    double balance;
    std::vector<Transaction> transactions;

public:
    Wallet(const std::string &name) : balance(0.0), ownerName(name) {}

    void deposit(double amount) {
        if (amount > 0) balance += amount; //  Safe deposit guard
    }

    void withdraw(double amount) {
        if (amount <= balance && amount > 0) balance -= amount; //  Prevent overdraft or invalid negative amount
    }

    double getBalance() const { return balance; }

    const std::string &getName() const { return ownerName; }

    const std::vector<Transaction>& getTransactions() const {
        return transactions; //  Return by const reference — avoids expensive copying of vector
    }

    void transfer(Wallet &other, double amount) {
        if (amount <= balance && amount > 0) {
            withdraw(amount);
            other.deposit(amount);

            //  Each wallet keeps its own copy of the transaction for record-keeping
            transactions.emplace_back(ownerName, other.getName(), amount); //  Copy overhead — consider lightweight TX objects or TX IDs only
            other.transactions.emplace_back(ownerName, other.getName(), amount);

            std::cout << "Transfer has been complete." << std::endl;
        } else {
            std::cout << "Insufficient balance or invalid amount." << std::endl;
        }
    }

    void displayTransactions() const {
        for (const auto &tx : transactions) {
            tx.display();
            std::cout << "------------------------" << std::endl;
        }
    }
};


