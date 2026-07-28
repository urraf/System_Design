#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <algorithm>

using namespace std;

// ---------- Vehicle Hierarchy ----------
enum class VehicleType { BIKE, CAR, TRUCK };

class Vehicle {
protected:
    string licensePlate;
    VehicleType type;

public:
Vehicle(string plate, VehicleType t)
    : licensePlate(std::move(plate)), type(t) {}

    VehicleType getType() const {
        return type;
    }

    virtual ~Vehicle() = default;
};

class Bike : public Vehicle {
public:
    Bike(string p) : Vehicle(p, VehicleType::BIKE) {}
};

class Car : public Vehicle {
public:
    Car(string p) : Vehicle(p, VehicleType::CAR) {}
};

class Truck : public Vehicle {
public:
    Truck(string p) : Vehicle(p, VehicleType::TRUCK) {}
};

// ---------- Factory ----------
class VehicleFactory {
public:
    static shared_ptr<Vehicle> createVehicle(VehicleType type, const string &plate) {
        switch (type) {
            case VehicleType::BIKE:
                return make_shared<Bike>(plate);
            case VehicleType::CAR:
                return make_shared<Car>(plate);
            case VehicleType::TRUCK:
                return make_shared<Truck>(plate);
        }
        throw invalid_argument("Unknown vehicle type");
    }
};

// ---------- Parking Spot Hierarchy ----------
enum class SpotSize { SMALL, MEDIUM, LARGE };

class ParkingSpot {
protected:
    string spotId;
    bool occupied = false;
    shared_ptr<Vehicle> parkedVehicle;

public:
ParkingSpot(string id)
    : spotId(std::move(id)) {}
    virtual ~ParkingSpot() = default;

    virtual SpotSize getSize() const = 0;

    bool isAvailable() const {
        return !occupied;
    }

    void assignVehicle(shared_ptr<Vehicle> v) {
        if (occupied)
            throw logic_error("Spot already occupied");

        parkedVehicle = v;
        occupied = true;
    }

    void removeVehicle() {
        parkedVehicle = nullptr;
        occupied = false;
    }

    string getId() const {
        return spotId;
    }
};

class SmallSpot : public ParkingSpot {
public:
    SmallSpot(string id) : ParkingSpot(id) {}

    SpotSize getSize() const override {
        return SpotSize::SMALL;
    }
};

class MediumSpot : public ParkingSpot {
public:
    MediumSpot(string id) : ParkingSpot(id) {}

    SpotSize getSize() const override {
        return SpotSize::MEDIUM;
    }
};

class LargeSpot : public ParkingSpot {
public:
    LargeSpot(string id) : ParkingSpot(id) {}

    SpotSize getSize() const override {
        return SpotSize::LARGE;
    }
};

SpotSize requiredSpotSize(VehicleType type) {
    switch (type) {
        case VehicleType::BIKE:
            return SpotSize::SMALL;
        case VehicleType::CAR:
            return SpotSize::MEDIUM;
        case VehicleType::TRUCK:
            return SpotSize::LARGE;
    }
    throw invalid_argument("Unknown vehicle type");
}

// ---------- Observer Pattern ----------
class Observer {
public:
    virtual void onNotify(int floorNum, int availableSpots) = 0;
    virtual ~Observer() = default;
};

class DisplayBoard : public Observer {
public:
    void onNotify(int floorNum, int availableSpots) override {
        cout << "[Display] Floor " << floorNum
             << ": " << availableSpots << " spots free\n";
    }
};

// ---------- Parking Floor ----------
class ParkingFloor {
private:
    int floorNumber;
    vector<shared_ptr<ParkingSpot>> spots;
    vector<Observer*> observers;

    void notifyObservers() {
        int available = count_if(
            spots.begin(),
            spots.end(),
            [](auto &s) {
                return s->isAvailable();
            });

        for (auto *obs : observers)
            obs->onNotify(floorNumber, available);
    }

public:
    ParkingFloor(int num) : floorNumber(num) {}

    void addSpot(shared_ptr<ParkingSpot> spot) {
        spots.push_back(spot);
    }

    void subscribe(Observer *obs) {
        observers.push_back(obs);
    }

    shared_ptr<ParkingSpot> findAvailableSpot(SpotSize size) {
        for (auto &s : spots) {
            if (s->isAvailable() && s->getSize() == size)
                return s;
        }
        return nullptr;
    }

    void occupySpot(shared_ptr<ParkingSpot> spot,
                    shared_ptr<Vehicle> v) {
        spot->assignVehicle(v);
        notifyObservers();
    }

    void freeSpot(shared_ptr<ParkingSpot> spot) {
        spot->removeVehicle();
        notifyObservers();
    }
};

// ---------- Strategy Pattern ----------
class PaymentStrategy {
public:
    virtual void pay(double amount) = 0;
    virtual ~PaymentStrategy() = default;
};

class CashPayment : public PaymentStrategy {
public:
    void pay(double amount) override {
        cout << "Paid Rs " << amount << " via Cash\n";
    }
};

class CardPayment : public PaymentStrategy {
public:
    void pay(double amount) override {
        cout << "Paid Rs " << amount << " via Card\n";
    }
};

// ---------- Ticket ----------
class Ticket {
private:
    shared_ptr<Vehicle> vehicle;
    shared_ptr<ParkingSpot> spot;
    chrono::steady_clock::time_point entryTime;

public:
    Ticket(shared_ptr<Vehicle> v,
           shared_ptr<ParkingSpot> s)
        : vehicle(v),
          spot(s),
          entryTime(chrono::steady_clock::now()) {}

    shared_ptr<ParkingSpot> getSpot() const {
        return spot;
    }

    double calculateFee() const {
        auto now = chrono::steady_clock::now();

        auto hours =
            chrono::duration_cast<chrono::hours>(now - entryTime).count();

        hours = max<long long>(hours, 1);

        double rate =
            (vehicle->getType() == VehicleType::TRUCK)
                ? 50.0
                : (vehicle->getType() == VehicleType::CAR)
                      ? 30.0
                      : 15.0;

        return hours * rate;
    }
};

// ---------- Singleton ----------
class ParkingLot {
private:
    vector<shared_ptr<ParkingFloor>> floors;

    ParkingLot() {}

    ParkingLot(const ParkingLot &) = delete;
    ParkingLot &operator=(const ParkingLot &) = delete;

public:
    static ParkingLot &getInstance() {
        static ParkingLot instance;
        return instance;
    }

    void addFloor(shared_ptr<ParkingFloor> floor) {
        floors.push_back(floor);
    }

    shared_ptr<Ticket> parkVehicle(shared_ptr<Vehicle> vehicle) {
        SpotSize needed = requiredSpotSize(vehicle->getType());

        for (auto &floor : floors) {
            auto spot = floor->findAvailableSpot(needed);

            if (spot) {
                floor->occupySpot(spot, vehicle);

                cout << "Parked vehicle at spot "
                     << spot->getId() << "\n";

                return make_shared<Ticket>(vehicle, spot);
            }
        }

        throw runtime_error("Parking Lot Full");
    }

    void unparkVehicle(shared_ptr<Ticket> ticket,
                       PaymentStrategy &payment) {
        double fee = ticket->calculateFee();

        payment.pay(fee);

        for (auto &floor : floors)
            floor->freeSpot(ticket->getSpot());
    }
};

int main() {
    ParkingLot &lot = ParkingLot::getInstance();

    auto floor1 = make_shared<ParkingFloor>(1);

    DisplayBoard board;

    floor1->subscribe(&board);

    floor1->addSpot(make_shared<SmallSpot>("S1"));
    floor1->addSpot(make_shared<MediumSpot>("M1"));
    floor1->addSpot(make_shared<LargeSpot>("L1"));

    lot.addFloor(floor1);

    auto truck =
        VehicleFactory::createVehicle(
            VehicleType::TRUCK,
            "DL8CAE7581");

    auto car =
        VehicleFactory::createVehicle(
            VehicleType::CAR,
            "DL01AB1234");

    auto ticket = lot.parkVehicle(car);
    cout<<"car ticket" << endl;
    cout<<ticket<<endl;

    

    CardPayment cardPay;

    lot.unparkVehicle(ticket, cardPay);

    return 0;
}