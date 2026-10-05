#pragma once

#include <box2d/box2d.h>

#include <memory>
#include <vector>

namespace ricochet {

constexpr float kStep = 1.0f / 60.0f;
constexpr int kRoundShots = 12;

struct TargetView {
    b2Vec2 position;
    float angle;
    bool scored;
};

class Simulation {
public:
    Simulation();
    ~Simulation();
    Simulation(Simulation&&) noexcept;
    Simulation& operator=(Simulation&&) noexcept;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    void reset();
    void setAim(float radians);
    void setCharge(float amount);
    bool fire();
    void tick();

    float aim() const;
    float charge() const;
    int shotsLeft() const;
    int score() const;
    int targetsScored() const;
    bool roundOver() const;
    std::vector<TargetView> targets() const;
    std::vector<b2Vec2> balls() const;
    bool ballInPlay() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace ricochet
