#include "simulation.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    ricochet::Simulation game;
    const auto initial = game.targets();
    check(initial.size() == 15, "15 targets at reset");
    check(game.shotsLeft() == 12 && game.score() == 0, "fresh round");

    // The successful lane is deliberately asymmetric: a level, full-power
    // shot reaches the pile and scores several targets on its far side.
    game.setAim(0.0f);
    game.setCharge(1.0f);
    check(game.fire(), "first shot accepted");
    check(game.shotsLeft() == 11, "one shot consumed");
    check(!game.fire(), "cooldown rejects immediate second shot");
    auto previousTargets = initial;
    for (int i = 0; i < 60 * 12; ++i) {
        game.tick();
        for (auto p : game.balls())
            check(p.x >= 0.7f && p.x <= 29.3f && p.y >= 0.7f && p.y <= 12.3f,
                  "balls stay inside all four walls, including the right half");
        const auto targets = game.targets();
        for (std::size_t j = 0; j < targets.size(); ++j) {
            const auto& target = targets[j];
            if (target.scored && !previousTargets[j].scored)
                check(target.position.x > 27.4f && target.position.y > 1.7f && target.position.y < 11.3f,
                      "a hit outside the goal lane cannot score");
        }
        previousTargets = targets;
    }
    check(game.score() > 0, "a level shot scores in the goal");
    int counted = 0;
    for (const auto& target : game.targets()) counted += target.scored;
    check(game.score() == counted * 100, "each scored target earns exactly 100 points");
    const auto moved = game.targets();
    bool anyTargetMoved = false;
    for (std::size_t i = 0; i < moved.size(); ++i)
        anyTargetMoved |= std::hypot(moved[i].position.x - initial[i].position.x,
                                     moved[i].position.y - initial[i].position.y) > 0.1f;
    check(anyTargetMoved, "Box2D collisions move the pile");
    const int scoredPoints = game.score();

    game.reset();
    check(game.score() == 0 && game.shotsLeft() == 12, "reset restores round");
    game.setAim(-9.0f);
    check(std::abs(game.aim() + 0.85f) < 0.001f, "negative aim clamp");
    game.setAim(9.0f);
    check(std::abs(game.aim() - 0.85f) < 0.001f, "positive aim clamp");
    game.setCharge(0.0f);
    check(std::abs(game.charge() - 0.15f) < 0.001f, "minimum charge");
    game.setCharge(9.0f);
    check(std::abs(game.charge() - 1.0f) < 0.001f, "maximum charge");
    check(game.fire(), "angled shot accepted");
    for (int i = 0; i < 41; ++i) game.tick();
    check(!game.fire(), "cooldown still active at tick 41");
    game.tick();
    check(game.fire(), "cooldown ends at tick 42");
    check(game.shotsLeft() == 10, "rejected shot does not consume ammunition");
    game.reset();
    for (int i = 0; i < 120; ++i) game.tick();
    check(game.score() == 0, "idle targets do not score without a shot");
    for (int tick = 0; tick < 3000 && !game.roundOver(); ++tick) {
        if (tick % 60 == 0) game.fire();
        game.tick();
        check(game.shotsLeft() >= 0, "shots never go negative while balls remain in play");
    }
    check(game.roundOver(), "round ends after ammunition and balls are exhausted, or all targets scored");
    check(!game.fire(), "completed round rejects shots");
    std::cout << "Box2D simulation: asymmetric shot scored " << scoredPoints
              << "; arena bounds, scoring, reset, clamps and cooldown boundaries passed\n";
}
