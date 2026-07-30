/*
 * ============================================================================
 *  LLD — Movie Ticket Booking System (BookMyShow)
 * ============================================================================
 *
 *  Entity draw order (topological sort of dependencies):
 *
 *  Tier 0 (pure data)  →  Seat, Movie              (depend on nothing)
 *  Tier 1 (containers) →  Screen, Theater           (hold Tier-0 objects)
 *  Tier 2 (central)    →  Show                      (references Movie, Screen, Seat)
 *  Tier 3 (interfaces) →  PaymentStrategy, Observer  (variation points)
 *  Tier 4 (orchestrator) → BookingManager            (uses everything above)
 *  Data class           → Booking                    (the receipt, created last)
 *
 *  Design Patterns Used:
 *    1. Strategy  → PaymentStrategy (CardPayment, UpiPayment)
 *    2. Observer  → Observer / BookingNotifier
 *    3. Mutex per-Show → thread-safe seat locking (NOT a global lock)
 *
 *  Key Design Insight:
 *    Seat availability is PER-SHOW, not a property of the physical Seat.
 *    Show owns map<string, SeatStatus>, NOT Seat owning a bool isBooked.
 * ============================================================================
 */

#include <iostream>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <stdexcept>

// ============================================================================
//  ENUMS
// ============================================================================

// Seat categories — each has a different price
enum class SeatCategory { SILVER, GOLD, PLATINUM };

// Three-state model for seat availability (per-show, NOT per-seat)
//   AVAILABLE → LOCKED → BOOKED
//   LOCKED can expire back to AVAILABLE if payment isn't completed
enum class SeatStatus { AVAILABLE, LOCKED, BOOKED };

// Helper: returns price based on seat category
// WHY a free function? — It's pure data mapping, doesn't belong to any class.
//   In an interview, keeps classes focused (Single Responsibility).
double priceForCategory(SeatCategory cat) {
    switch (cat) {
        case SeatCategory::SILVER:   return 150.0;
        case SeatCategory::GOLD:     return 250.0;
        case SeatCategory::PLATINUM: return 400.0;
    }
    throw std::invalid_argument("Unknown seat category");
}


// ============================================================================
//  TIER 0 — Pure Data / Physical Entities (depend on nothing)
// ============================================================================

/*
 * Seat — represents a PHYSICAL seat in a screen.
 *
 * From your UML:
 *   - seatNumber : string
 *   - category   : SeatCategory
 *   + getNumber() / getCategory()
 *
 * IMPORTANT: Seat does NOT have isBooked / status.
 *   That state lives in Show's map<string, SeatStatus>, because the same
 *   physical seat (e.g., A1 on Screen 1) can be available for the 3 PM show
 *   but booked for the 6 PM show.
 */
class Seat {
private:
    std::string seatNumber;     // e.g., "A1", "B3"
    SeatCategory category;      // SILVER / GOLD / PLATINUM

public:
    Seat(std::string num, SeatCategory cat)
        : seatNumber(std::move(num)), category(cat) {}

    std::string getNumber() const { return seatNumber; }
    SeatCategory getCategory() const { return category; }
};


/*
 * Movie — represents a movie (title, duration, language, etc.)
 *
 * From your UML:
 *   - title : string
 *   + getTitle() : string
 *
 * WHY a separate class? — Even though it's tiny now, it's an independent
 *   real-world entity. In a real system it would grow (genre, rating, etc.).
 */
class Movie {
private:
    std::string title;

public:
    Movie(std::string t) : title(std::move(t)) {}

    std::string getTitle() const { return title; }
};


// ============================================================================
//  TIER 1 — Simple Containers (hold Tier-0 objects)
// ============================================================================

/*
 * Screen — represents a physical screen in a theater.
 *
 * From your UML:
 *   - screenId : string
 *   - seats    : vector<Seat>       ◇── aggregation ("has many" Seats)
 *   + getSeats() : Seat[]
 *
 * A Screen owns a FIXED seat layout — it doesn't change between shows.
 * Different Shows on the same Screen share the same physical seats but
 * track their own availability via Show's seatStatus map.
 */
class Screen {
private:
    std::string screenId;       // e.g., "SCR1"
    std::vector<Seat> seats;    // fixed seat layout for this screen

public:
    Screen(std::string id, std::vector<Seat> s)
        : screenId(std::move(id)), seats(std::move(s)) {}

    std::string getId() const { return screenId; }

    // Returns const ref to avoid unnecessary copies
    const std::vector<Seat>& getSeats() const { return seats; }
};


/*
 * Theater — represents a physical theater (multiplex).
 *
 * From your UML:
 *   - screens : vector<Screen>      ◇── aggregation ("has many" Screens)
 *   + addScreen(screen)
 *
 * WHY aggregation (◇) and not composition (◆)?
 *   A Screen could theoretically exist independently (e.g., rented out),
 *   but in practice this distinction rarely matters in LLD interviews.
 *   The key point: Theater "has many" Screens.
 */
class Theater {
private:
    std::string name;
    std::vector<Screen> screens;

public:
    Theater(std::string n) : name(std::move(n)) {}

    // Adds a screen to this theater
    void addScreen(Screen screen) {
        screens.push_back(std::move(screen));
    }

    const std::vector<Screen>& getScreens() const { return screens; }
    std::string getName() const { return name; }
};


// ============================================================================
//  TIER 2 — Central / Transactional Entity
// ============================================================================

/*
 * Show — A movie playing on a specific Screen at a specific time.
 *
 * From your UML (the blue box — the most important class):
 *   - movie      : Movie
 *   - screen     : Screen&              ──▶ association to Screen
 *   - time       : string
 *   - seatStatus : map<string, SeatStatus>   ← PER-SHOW seat availability
 *   - mtx        : mutex                     ← guards seatStatus
 *
 *   + lockSeats(seats[]) : bool
 *   + confirmBooking(seats[])
 *   + releaseSeats(seats[])
 *   + getScreen() / getMovieTitle()
 *
 * *** THIS IS THE KEY DESIGN POINT OF THE ENTIRE PROBLEM ***
 *
 * WHY does Show own seatStatus (not Seat)?
 *   The same physical Seat "A1" can be AVAILABLE for the 3 PM show and
 *   BOOKED for the 6 PM show. If Seat owned a bool isBooked, you'd have
 *   no way to represent this — classic LLD trap.
 *
 * WHY mutex PER-SHOW (not a global mutex)?
 *   A global lock would serialize bookings for EVERY show in EVERY theater,
 *   even completely unrelated ones. Per-show locking means Show A's booking
 *   traffic never blocks Show B's. Granularity matters.
 *
 * WHY a three-state model (AVAILABLE → LOCKED → BOOKED)?
 *   If we directly mark a seat BOOKED on selection, and the user abandons
 *   checkout, that seat is stuck unavailable forever. LOCKED lets us
 *   auto-expire back to AVAILABLE if payment doesn't complete in ~5 min.
 */
class Show {
private:
    Movie movie;
    Screen& screen;              // reference to the physical screen
    std::string time;            // e.g., "6:00 PM"

    // Per-show seat status map: seatNumber → status
    // This is the core data structure — NOT a field on Seat
    std::map<std::string, SeatStatus> seatStatus;

    // Mutex guards seatStatus for THIS show only
    // NOT a global mutex — each show has its own lock
    std::mutex mtx;

public:
    Show(Movie m, Screen& s, std::string t)
        : movie(std::move(m)), screen(s), time(std::move(t))
    {
        // Initialize all seats in this screen as AVAILABLE for this show
        for (const auto& seat : screen.getSeats()) {
            seatStatus[seat.getNumber()] = SeatStatus::AVAILABLE;
        }
    }

    /*
     * lockSeats — Atomically check-and-lock a set of seats.
     *
     * This is the CRITICAL SECTION of the entire design.
     *
     * WHY atomic check-and-lock?
     *   Without atomicity, two threads can both pass the "is available?" check
     *   before either marks the seat as LOCKED — classic TOCTOU (Time-Of-Check
     *   to Time-Of-Use) race condition.
     *
     *   Thread A: check A1 → AVAILABLE ✓
     *   Thread B: check A1 → AVAILABLE ✓  (both pass!)
     *   Thread A: lock A1
     *   Thread B: lock A1  ← DOUBLE BOOKING!
     *
     *   lock_guard ensures only one thread enters this block at a time.
     *
     * WHY lock ALL-or-NOTHING?
     *   If a user selects seats A1+A2 and A2 is already taken, we don't
     *   lock A1 alone — that would leave A1 stranded as LOCKED with no
     *   booking to confirm or release it.
     *
     * Returns: true if all seats successfully locked, false otherwise.
     */
    bool lockSeats(const std::vector<std::string>& seatNumbers) {
        std::lock_guard<std::mutex> lock(mtx);  // acquire lock for this show

        // Phase 1: CHECK — are all requested seats available?
        for (const auto& num : seatNumbers) {
            if (seatStatus.find(num) == seatStatus.end()) return false;  // invalid seat
            if (seatStatus[num] != SeatStatus::AVAILABLE) return false;  // already taken
        }

        // Phase 2: LOCK — mark all as LOCKED (only reached if all are available)
        for (const auto& num : seatNumbers) {
            seatStatus[num] = SeatStatus::LOCKED;
        }

        return true;
    }

    /*
     * confirmBooking — Permanently mark seats as BOOKED after payment succeeds.
     *
     * Called ONLY after payment is confirmed.
     * Transitions: LOCKED → BOOKED
     */
    void confirmBooking(const std::vector<std::string>& seatNumbers) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& num : seatNumbers) {
            seatStatus[num] = SeatStatus::BOOKED;
        }
    }

    /*
     * releaseSeats — Release LOCKED seats back to AVAILABLE.
     *
     * Called when:
     *   1. Payment fails (exception in bookSeats)
     *   2. User abandons checkout (5-min timer expires)
     *
     * Only releases seats that are LOCKED (not BOOKED — booked seats are permanent).
     */
    void releaseSeats(const std::vector<std::string>& seatNumbers) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& num : seatNumbers) {
            if (seatStatus[num] == SeatStatus::LOCKED) {
                seatStatus[num] = SeatStatus::AVAILABLE;
            }
        }
    }

    // Getters
    Screen& getScreen() { return screen; }
    std::string getMovieTitle() const { return movie.getTitle(); }
    std::string getTime() const { return time; }

    // Utility: print current seat status (for demo/debugging)
    void printSeatStatus() const {
        std::cout << "  Show: " << movie.getTitle() << " @ " << time << "\n";
        for (const auto& [seatNum, status] : seatStatus) {
            std::string statusStr;
            switch (status) {
                case SeatStatus::AVAILABLE: statusStr = "AVAILABLE"; break;
                case SeatStatus::LOCKED:    statusStr = "LOCKED";    break;
                case SeatStatus::BOOKED:    statusStr = "BOOKED";    break;
            }
            std::cout << "    Seat " << seatNum << " → " << statusStr << "\n";
        }
    }
};


// ============================================================================
//  TIER 3 — Variation Points (Interfaces / Strategy & Observer)
// ============================================================================

/*
 * PaymentStrategy — <<interface>> for pluggable payment methods.
 *
 * From your UML:
 *   + pay(amount: double)
 *   "chosen by caller, not manager"
 *
 * WHY Strategy pattern?
 *   The interviewer said "multiple payment methods, extensible."
 *   This is the same axis of variation as Splitwise's SplitStrategy:
 *   - BookingManager calls paymentStrategy->pay(amount) without knowing
 *     which concrete payment method is used.
 *   - Adding a new payment method (e.g., WalletPayment) requires ZERO
 *     changes to BookingManager → Open/Closed Principle (OCP).
 *
 * WHY the caller chooses the strategy, not the manager?
 *   The user picks their payment method at checkout time.
 *   BookingManager shouldn't decide for them — it just delegates.
 */
class PaymentStrategy {
public:
    virtual void pay(double amount) = 0;        // pure virtual → must override
    virtual ~PaymentStrategy() = default;        // virtual dtor for proper cleanup
};

// Concrete Strategy: Card Payment
class CardPayment : public PaymentStrategy {
public:
    void pay(double amount) override {
        std::cout << "  [Payment] Paid ₹" << amount << " via Credit/Debit Card\n";
    }
};

// Concrete Strategy: UPI Payment
class UpiPayment : public PaymentStrategy {
public:
    void pay(double amount) override {
        std::cout << "  [Payment] Paid ₹" << amount << " via UPI\n";
    }
};


/*
 * Observer — <<interface>> for booking event notifications.
 *
 * From your UML:
 *   + onBookingConfirmed(user, amt)
 *
 * WHY Observer pattern?
 *   BookingManager shouldn't hardcode "send SMS" or "send email."
 *   It just notifies all subscribed observers when a booking is confirmed.
 *   Adding a new notification channel (email, push, etc.) requires
 *   ZERO changes to BookingManager → OCP again.
 */
class Observer {
public:
    virtual void onBookingConfirmed(const std::string& userId, double amount) = 0;
    virtual ~Observer() = default;
};

// Concrete Observer: sends a booking confirmation notification
class BookingNotifier : public Observer {
public:
    void onBookingConfirmed(const std::string& userId, double amount) override {
        std::cout << "  [Notify] " << userId
                  << " → Booking confirmed! Amount paid: ₹" << amount << "\n";
    }
};


// ============================================================================
//  DATA CLASS — Booking (the receipt)
// ============================================================================

/*
 * Booking — represents a confirmed reservation.
 *
 * From your UML:
 *   - userId      : string
 *   - seatNumbers : vector<string>
 *   - amount      : double
 *
 * WHY a separate class (not just a return value)?
 *   A Booking is a domain entity — it might be stored in a DB, referenced
 *   for cancellation, or shown in "My Bookings." It deserves its own class.
 */
class Booking {
private:
    std::string userId;
    std::vector<std::string> seatNumbers;
    double amount;

public:
    Booking(std::string user, std::vector<std::string> seats, double amt)
        : userId(std::move(user)), seatNumbers(std::move(seats)), amount(amt) {}

    // Getters
    std::string getUserId() const { return userId; }
    const std::vector<std::string>& getSeatNumbers() const { return seatNumbers; }
    double getAmount() const { return amount; }

    void printBooking() const {
        std::cout << "  [Booking] User: " << userId << " | Seats: ";
        for (const auto& s : seatNumbers) std::cout << s << " ";
        std::cout << "| Amount: ₹" << amount << "\n";
    }
};


// ============================================================================
//  TIER 4 — Orchestrator (uses everything above)
// ============================================================================

/*
 * BookingManager — the orchestrator that coordinates the entire booking flow.
 *
 * From your UML:
 *   - observers : vector<Observer*>
 *   + subscribe(observer)
 *   + bookSeats(show, userId, seatNumbers, categories, payment)
 *
 *   Flow: locks → pays → confirms → notifies
 *          (releases lock if payment throws)
 *
 * WHY NOT a Singleton?
 *   Same reasoning as ExpenseManager in Splitwise:
 *   - No physical constraint forces exactly one instance.
 *   - We want it testable via injected dependencies (PaymentStrategy, Observer).
 *   - Singleton makes unit testing painful (global state, can't swap mocks).
 *   - "not a Singleton — injected deps" is written right on your UML.
 *
 * WHY does BookingManager use Show (not Seat) for locking?
 *   Seat availability is per-show. BookingManager calls show.lockSeats(),
 *   not seat.lock(). This is the straight vertical line in your UML
 *   from BookingManager to Show — the most important relationship.
 */
class BookingManager {
private:
    std::vector<Observer*> observers;   // subscribed notification observers

public:
    // Subscribe an observer to receive booking notifications
    void subscribe(Observer* obs) {
        observers.push_back(obs);
    }

    /*
     * bookSeats — the main booking flow.
     *
     * Parameters:
     *   show        — the Show to book seats for
     *   userId      — who is booking
     *   seatNumbers — which seats to book (e.g., {"A1", "A2"})
     *   categories  — category of each seat (for price calculation)
     *   payment     — the PaymentStrategy chosen by the user
     *
     * Flow (matches your UML annotation):
     *   1. LOCK seats atomically (check + mark as LOCKED)
     *   2. PAY via the injected PaymentStrategy
     *   3. CONFIRM booking (mark seats as BOOKED)
     *   4. NOTIFY all subscribed observers
     *
     * If payment FAILS (exception):
     *   → RELEASE locked seats back to AVAILABLE
     *   → Re-throw the exception to the caller
     *
     * IMPORTANT: The mutex is only held during lockSeats/confirmBooking/
     *   releaseSeats (microseconds each). Payment happens OUTSIDE the lock.
     *   If we held the lock during payment (which can take 10+ seconds for
     *   a gateway call), it would serialize ALL bookings for that show —
     *   terrible throughput.
     */
    std::shared_ptr<Booking> bookSeats(
        Show& show,
        const std::string& userId,
        const std::vector<std::string>& seatNumbers,
        const std::vector<SeatCategory>& categories,
        PaymentStrategy& payment)
    {
        // ── Step 1: LOCK seats atomically ──
        // If any seat is already LOCKED or BOOKED, this returns false
        // and we don't proceed (no partial locking).
        if (!show.lockSeats(seatNumbers)) {
            throw std::runtime_error("Booking failed: one or more seats already taken!");
        }

        try {
            // ── Step 2: Calculate total price ──
            double total = 0;
            for (auto cat : categories) {
                total += priceForCategory(cat);
            }

            // ── Step 3: PAY via Strategy (happens OUTSIDE the mutex) ──
            payment.pay(total);

            // ── Step 4: CONFIRM booking (seats transition LOCKED → BOOKED) ──
            show.confirmBooking(seatNumbers);

            // ── Step 5: NOTIFY all observers ──
            for (auto* obs : observers) {
                obs->onBookingConfirmed(userId, total);
            }

            // ── Step 6: Create and return the Booking receipt ──
            return std::make_shared<Booking>(userId, seatNumbers, total);

        } catch (...) {
            // Payment failed! Release the locked seats so they go back
            // to AVAILABLE — don't leave them stranded as LOCKED forever.
            show.releaseSeats(seatNumbers);
            throw;  // re-throw so caller knows booking failed
        }
    }
};


// ============================================================================
//  MAIN — Demo / Sample Usage
// ============================================================================

int main() {
    std::cout << "=== Movie Ticket Booking System (BookMyShow) ===\n\n";

    // ── 1. Create physical seats (Tier 0) ──
    std::vector<Seat> seats = {
        Seat("A1", SeatCategory::SILVER),
        Seat("A2", SeatCategory::SILVER),
        Seat("B1", SeatCategory::GOLD),
        Seat("B2", SeatCategory::GOLD),
        Seat("C1", SeatCategory::PLATINUM),
    };

    // ── 2. Create a screen with those seats (Tier 1) ──
    Screen screen1("SCR1", seats);

    // ── 3. Create a theater and add the screen (Tier 1) ──
    Theater theater("PVR Cinemas");
    theater.addScreen(Screen("SCR1", seats));   // theater "has many" screens

    // ── 4. Create a show — movie + screen + time (Tier 2) ──
    Show show1(Movie("Inception"), screen1, "6:00 PM");
    Show show2(Movie("Inception"), screen1, "9:00 PM");  // same screen, different time

    // ── 5. Create payment strategies (Tier 3) ──
    CardPayment cardPayment;
    UpiPayment upiPayment;

    // ── 6. Create observer and subscribe (Tier 3) ──
    BookingNotifier notifier;

    // ── 7. Create the orchestrator — NOT a Singleton (Tier 4) ──
    BookingManager manager;
    manager.subscribe(&notifier);   // register observer

    // ─────────────────────────────────────────────────
    //  SCENARIO 1: User "farhan" books Gold seats B1, B2 for 6 PM show
    // ─────────────────────────────────────────────────
    std::cout << "── Scenario 1: farhan books B1, B2 (Gold) for 6 PM ──\n";
    try {
        auto booking = manager.bookSeats(
            show1, "farhan",
            {"B1", "B2"},                                       // seat numbers
            {SeatCategory::GOLD, SeatCategory::GOLD},           // categories
            cardPayment                                         // pay by card
        );
        booking->printBooking();
    } catch (const std::exception& e) {
        std::cout << "  ERROR: " << e.what() << "\n";
    }

    std::cout << "\n";

    // ─────────────────────────────────────────────────
    //  SCENARIO 2: User "alice" tries to book B1 (already booked!)
    //  This demonstrates concurrent-safety — B1 is BOOKED, so it fails.
    // ─────────────────────────────────────────────────
    std::cout << "── Scenario 2: alice tries B1 (already booked) for 6 PM ──\n";
    try {
        auto booking = manager.bookSeats(
            show1, "alice",
            {"B1"},
            {SeatCategory::GOLD},
            upiPayment
        );
        booking->printBooking();
    } catch (const std::exception& e) {
        std::cout << "  ERROR: " << e.what() << "\n";
    }

    std::cout << "\n";

    // ─────────────────────────────────────────────────
    //  SCENARIO 3: alice books B1 for the 9 PM show (different show, same seat)
    //  This proves seat status is PER-SHOW, not per-Seat.
    // ─────────────────────────────────────────────────
    std::cout << "── Scenario 3: alice books B1 (Gold) for 9 PM (different show!) ──\n";
    try {
        auto booking = manager.bookSeats(
            show2, "alice",                    // show2 = 9 PM (different from show1)
            {"B1"},
            {SeatCategory::GOLD},
            upiPayment                          // pay by UPI this time
        );
        booking->printBooking();
    } catch (const std::exception& e) {
        std::cout << "  ERROR: " << e.what() << "\n";
    }

    std::cout << "\n";

    // ─────────────────────────────────────────────────
    //  SCENARIO 4: Print seat status for both shows
    // ─────────────────────────────────────────────────
    std::cout << "── Final Seat Status ──\n";
    show1.printSeatStatus();
    std::cout << "\n";
    show2.printSeatStatus();

    return 0;
}
