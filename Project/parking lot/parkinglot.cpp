#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
using namespace std;

enum class vehicleType {Bike, Car, Truck};

class Vehicle{
protected:
    vehicleType type;
    string numberPlate;
public: 
    Vehicle(vehicleType t, string plate){
        this->type  = t;
        this->numberPlate =  plate;
    }
    string getnumberplate() const {
        return numberPlate;
    }
    vehicleType getType() const {return type;}

    virtual ~Vehicle() = default;
};

class Car : public Vehicle{
public: 
    Car(string plate) : Vehicle(vehicleType::Car,  plate){}
};
class Bike : public Vehicle{
public:
     Bike(string plate) : Vehicle(vehicleType::Bike,plate){}
};
class Truck : public Vehicle{
public:
     Truck(string plate) : Vehicle(vehicleType::Truck,plate){}
};

//factory
class VehicleFactor{
public: 
   static Vehicle* createVehicle(vehicleType type, const string plate){
        switch(type){
            case vehicleType::Bike:
                return new Bike(plate);
            case vehicleType::Car:
                return new Car(plate);
            case vehicleType::Truck:
                return new Truck(plate);
            default:
                 throw invalid_argument("invalid car type");
        }
       
    }
};

//parking spot
enum class SpotSize{SMALL, MEDIUM , LARGE};
class ParkingSpot{
protected:
    int spotid;
    bool isOccupied = false;
    Vehicle* parkedVehicle = nullptr;
public:
    ParkingSpot(int id){
        this->spotid = id;
    }
    
    
    void assignVehicle(Vehicle* vehicle){
        if(isOccupied){
            throw logic_error("spot already filled");
        }
        parkedVehicle = vehicle;
        isOccupied =  true;
    }
    void removeVehicle(){
        parkedVehicle =  nullptr;
        isOccupied = false;
    }
    int getspotid(){return spotid;}

    
    bool isavailable(){return !isOccupied;}
    virtual SpotSize getSize() const = 0;
    virtual ~ParkingSpot() =  default;
};

class Smallspot : public ParkingSpot {
public:
    Smallspot(int id) : ParkingSpot(id) {}
    SpotSize getSize() const override {
        return SpotSize::SMALL;
    }
};
class MediumSpot : public ParkingSpot {
public:
    MediumSpot(int id) : ParkingSpot(id) {}
    SpotSize getSize() const override {
        return SpotSize::MEDIUM;
    }
};
class LargeSpot : public ParkingSpot {
public:
    LargeSpot(int id) : ParkingSpot(id) {}
    SpotSize getSize() const override {
        return SpotSize::LARGE;
    }
};

SpotSize requiredspot(vehicleType type){
    switch(type){
        case vehicleType::Bike:
            return SpotSize::SMALL;
        case vehicleType::Car:
            return SpotSize::MEDIUM;
        case vehicleType::Truck:
            return SpotSize::LARGE;
        default:
            throw invalid_argument("unknown vehicle type");
    }
}

class Observer{
public:
    virtual void notify(int floorid, int availablespots) = 0;
    virtual ~Observer() =  default;
};
class DisplayBoard : public Observer{
public:
    void notify(int floorid, int availableSpots) override{
         cout << "[Display] Floor " << floorid
             << ": " << availableSpots << " spots free\n";
    }
};


class ParkingFloor{
protected: 
    vector<ParkingSpot*> spots;
    int floorid;
    vector<Observer*> observers;
    void notifyObserver(){
      int availalbespot = 0;  
      for(auto sp : spots){
        if(sp->isavailable()){
            availalbespot++;
        }
      }
      for(auto ob : observers){
        ob->notify(floorid, availalbespot);
      }
    }

public:
    ParkingFloor(int id){this->floorid = id;}

    void addspot(ParkingSpot* spot){
        spots.push_back(spot);
    }
    
    void subscribe(Observer* observer){
        observers.push_back(observer);
    }

    ParkingSpot* findavailableSpot(SpotSize spotsize){
        for(auto spot: spots){
            if(spot->isavailable() && spot->getSize() == spotsize) return spot;
        }
        return nullptr;
    }

    void occupySpot(ParkingSpot* spot, Vehicle* vehicle){
        spot->assignVehicle(vehicle);
        notifyObserver();
    }   

    bool hashspot(ParkingSpot* sp)
    {
        for(auto spot: spots){
            if(spot ==  sp) return true;
        }
        return false;
    }
    void freespot(ParkingSpot* spot){
        spot->removeVehicle();
        notifyObserver();
    }
    
};


//payment strategy
class PaymentStrategy{
public:
    virtual void pay(double amount) = 0;
    virtual ~PaymentStrategy() = default;
};

class UPI : public PaymentStrategy{
public:
    void pay(double amount) override{
        cout<<"paid throgh upi";
    }
};
class CARD : public PaymentStrategy{
public:
    void pay(double amount) override{
        cout<<"paid throgh CARD";
    }
};

class CASH : public PaymentStrategy{
public:
    void pay(double amount) override{
        cout<<"paid throgh CASH";
    }
};

class Ticket{
private:
    Vehicle *vehicle;
    ParkingSpot *Spot;
     using TimePoint = chrono::steady_clock::time_point;
    TimePoint entryTime;
public:
    Ticket(Vehicle *vehicle, ParkingSpot* spot){
        this->vehicle = vehicle;
        this->Spot = spot;
        entryTime = chrono::steady_clock::now();
    }
    ParkingSpot * getspot() {return Spot;}
    double calculatefee(){
         auto now = chrono::steady_clock::now();

        auto hours =
            chrono::duration_cast<chrono::hours>(now - entryTime).count();

        hours = max<long long>(hours, 1);

        double rate =
            (vehicle->getType() == vehicleType::Truck)
                ? 50.0
                : (vehicle->getType() == vehicleType::Car)
                      ? 30.0
                      : 15.0;

        return hours * rate;
    }

};

//singleton
class ParkingLot{
private:    
    vector<ParkingFloor*> floors;
    static ParkingLot* instance;
    static mutex mtx;
    ParkingLot(){}
public:
    ParkingLot &operator = (const ParkingLot&) = delete;
    ParkingLot(const ParkingLot*) = delete;

   static ParkingLot* getInstance(){
    if(instance == nullptr){
        lock_guard<mutex> lock(mtx);

        if(instance == nullptr){
            instance = new ParkingLot();
        }
    }

    return instance;
}

     void addFloor(ParkingFloor* floor) {
        floors.push_back(floor);
    }
    Ticket parkvehicle(Vehicle* vehicle){
        SpotSize spotsizeneeded = requiredspot(vehicle->getType());
        for(auto floor: floors){
            auto spot  = floor->findavailableSpot(spotsizeneeded);
            if(spot){
                floor->occupySpot(spot,vehicle);
                 cout << "Parked vehicle at spot "
                     << spot->getspotid() << "\n";
                return Ticket(vehicle, spot);
            }
        }
        throw runtime_error("parking lot full");
    }    

    void unparkVehicle(Ticket* ticket, PaymentStrategy* strategy){
        double fee = ticket->calculatefee();
        strategy->pay(fee);
        auto spot = ticket->getspot();
        for(auto floor : floors)
        {   
            if(floor->hashspot(spot)){
                floor->freespot(spot);
                break;
            }

        }
    }

};

ParkingLot* ParkingLot::instance =nullptr;
mutex ParkingLot::mtx;


int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);   
    
    ParkingLot * lot = ParkingLot::getInstance();
    auto floor1 = new ParkingFloor(1);
    DisplayBoard* board = new DisplayBoard();
    floor1->subscribe(board);
    floor1->addspot(new Smallspot(1));
    floor1->addspot(new Smallspot(2));
    floor1->addspot(new MediumSpot(3));
    floor1->addspot(new LargeSpot(4));

    lot->addFloor(floor1);

    auto Truck  = VehicleFactor::createVehicle(vehicleType::Truck, "DLTRUCK 0000");
    auto Car = VehicleFactor::createVehicle(vehicleType::Car, "DL8CAE7581");
    auto bike = VehicleFactor::createVehicle(vehicleType::Bike, "DL3SEV8826");

    auto Ticket =  lot->parkvehicle(Car);
    auto ticketbike = lot->parkvehicle(bike);
    auto car2 = lot->parkvehicle(Car);

    

    CARD* cardstrategy = new CARD();

    lot->unparkVehicle(&Ticket, cardstrategy);



    return 0;
}