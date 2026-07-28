#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <cmath>

// ---------- User ----------

class User {
    std::string id, name;
public:
    User(std::string id, std::string name) : id(std::move(id)), name(std::move(name)) {}
    std::string getId() const { return id; }
    std::string getName() const { return name; }
    bool operator<(const User& o) const { return id < o.id; }   // needed for map keys
};

// ---------- Split (data, not behavior) ----------
struct Split {
    User user;
    double amountOwed;
};

// ---------- Strategy: SplitStrategy ----------
class SplitStrategy {
public:
    virtual std::vector<Split> calculateSplits(double amount, const std::vector<User>& participants,
                                                const std::vector<double>& values) = 0;
    virtual ~SplitStrategy() = default;
};

class EqualSplitStrategy : public SplitStrategy {
public:
    std::vector<Split> calculateSplits(double amount, const std::vector<User>& participants,
                                        const std::vector<double>&) override {
        std::vector<Split> result;
        double share = amount / participants.size();
        for (auto& u : participants) result.push_back({u, share});
        return result;
    }
};

class ExactSplitStrategy : public SplitStrategy {
public:
    std::vector<Split> calculateSplits(double amount, const std::vector<User>& participants,
                                        const std::vector<double>& values) override {
        if (values.size() != participants.size())
            throw std::invalid_argument("Exact amounts must be given for every participant");
        double sum = 0;
        for (double v : values) sum += v;
        if (std::abs(sum - amount) > 0.01)
            throw std::invalid_argument("Exact amounts must sum to total expense");

        std::vector<Split> result;
        for (size_t i = 0; i < participants.size(); i++) result.push_back({participants[i], values[i]});
        return result;
    }
};

class PercentSplitStrategy : public SplitStrategy {
public:
    std::vector<Split> calculateSplits(double amount, const std::vector<User>& participants,
                                        const std::vector<double>& percentages) override {
        if (percentages.size() != participants.size())
            throw std::invalid_argument("Percentages must be given for every participant");
        double sum = 0;
        for (double p : percentages) sum += p;
        if (std::abs(sum - 100.0) > 0.01)
            throw std::invalid_argument("Percentages must sum to 100");

        std::vector<Split> result;
        for (size_t i = 0; i < participants.size(); i++)
            result.push_back({participants[i], amount * percentages[i] / 100.0});
        return result;
    }
};

// ---------- Factory ----------
enum class SplitType { EQUAL, EXACT, PERCENT };

class SplitStrategyFactory {
public:
    static std::unique_ptr<SplitStrategy> create(SplitType type) {
        switch (type) {
            case SplitType::EQUAL:   return std::make_unique<EqualSplitStrategy>();
            case SplitType::EXACT:   return std::make_unique<ExactSplitStrategy>();
            case SplitType::PERCENT: return std::make_unique<PercentSplitStrategy>();
        }
        throw std::invalid_argument("Unknown split type");
    }
};

// ---------- Observer (notifications) ----------
class Observer {
public:
    virtual void onExpenseAdded(const User& affectedUser, double amount, const std::string& desc) = 0;
    virtual ~Observer() = default;
};

class NotificationService : public Observer {
public:
    void onExpenseAdded(const User& affectedUser, double amount, const std::string& desc) override {
        std::cout << "[Notify] " << affectedUser.getName() << ": you owe " << amount
                  << " for '" << desc << "'\n";
    }
};

// ---------- Expense ----------
class Expense {
    double amount;
    User paidBy;
    std::string description;
    std::vector<Split> splits;
public:
    Expense(double amt, User payer, std::string desc, std::vector<Split> splits)
        : amount(amt), paidBy(std::move(payer)), description(std::move(desc)), splits(std::move(splits)) {}

    const std::vector<Split>& getSplits() const { return splits; }
    const User& getPayer() const { return paidBy; }
    std::string getDescription() const { return description; }
};

// ---------- BalanceSheet (Ledger) ----------
class BalanceSheet {
    // balances[A][B] = amount B owes A (positive = B owes A)
    std::map<std::string, std::map<std::string, double>> balances;
public:
    void addDebt(const User& owedTo, const User& owedBy, double amount) {
        if (owedTo.getId() == owedBy.getId()) return;
        balances[owedTo.getId()][owedBy.getId()] += amount;
        balances[owedBy.getId()][owedTo.getId()] -= amount;   // keep symmetric
    }

    void showBalances(const User& user) const {
        auto it = balances.find(user.getId());
        if (it == balances.end()) { std::cout << user.getName() << ": no balances\n"; return; }
        for (auto& [otherId, amt] : it->second) {
            if (std::abs(amt) < 0.01) continue;
            if (amt > 0) std::cout << otherId << " owes " << user.getName() << ": " << amt << "\n";
            else std::cout << user.getName() << " owes " << otherId << ": " << -amt << "\n";
        }
    }
};

// ---------- ExpenseManager (orchestrator, NOT a singleton) ----------
class ExpenseManager {
    BalanceSheet& ledger;
    std::vector<Observer*> observers;

public:
    ExpenseManager(BalanceSheet& ledger) : ledger(ledger) {}   // dependency injected, not global

    void subscribe(Observer* obs) { observers.push_back(obs); }

    std::shared_ptr<Expense> addExpense(double amount, User payer, std::string desc,
                                         std::vector<User> participants, SplitType type,
                                         std::vector<double> values = {}) {
        auto strategy = SplitStrategyFactory::create(type);
        auto splits = strategy->calculateSplits(amount, participants, values);

        for (auto& split : splits) {
            if (split.user.getId() == payer.getId()) continue;
            ledger.addDebt(payer, split.user, split.amountOwed);
            for (auto* obs : observers) obs->onExpenseAdded(split.user, split.amountOwed, desc);
        }
        return std::make_shared<Expense>(amount, payer, desc, splits);
    }
};

int main() {
    User alice("u1", "Alice"), bob("u2", "Bob"), carol("u3", "Carol");

    BalanceSheet ledger;
    ExpenseManager manager(ledger);
    NotificationService notifier;
    manager.subscribe(&notifier);

    // Equal split
    manager.addExpense(300.0, alice, "Dinner", {alice, bob, carol}, SplitType::EQUAL);

    // Percentage split
    manager.addExpense(1000.0, bob, "Rent", {alice, bob, carol}, SplitType::PERCENT, {40, 30, 30});

    ledger.showBalances(alice);
}