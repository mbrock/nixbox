#include "simulation.hpp"

#include <algorithm>
#include <cmath>

namespace ricochet {
namespace {
constexpr float kBallRadius = 0.28f;
constexpr float kTargetSize = 0.58f;
constexpr int kCooldownTicks = 42;
constexpr int kMaxBalls = 8;

struct Target {
    b2BodyId body;
    bool scored = false;
};
struct Ball {
    b2BodyId body;
    int age = 0;
};

b2ShapeDef shapeDef(float friction, float restitution) {
    b2ShapeDef def = b2DefaultShapeDef();
    def.material.friction = friction;
    def.material.restitution = restitution;
    return def;
}
} // namespace

struct Simulation::State {
    b2WorldId world = b2_nullWorldId;
    std::vector<Target> targets;
    std::vector<Ball> balls;
    float aim = 0.0f;
    float charge = 0.55f;
    int shots = kRoundShots;
    int score = 0;
    int cooldown = 0;

    ~State() {
        if (B2_IS_NON_NULL(world)) b2DestroyWorld(world);
    }
};

Simulation::Simulation() : state_(std::make_unique<State>()) { reset(); }
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;

void Simulation::reset() {
    if (B2_IS_NON_NULL(state_->world)) b2DestroyWorld(state_->world);
    state_->targets.clear();
    state_->balls.clear();
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, 0.0f};
    state_->world = b2CreateWorld(&worldDef);
    state_->aim = 0.0f;
    state_->charge = 0.55f;
    state_->shots = kRoundShots;
    state_->score = 0;
    state_->cooldown = 0;

    const auto makeWall = [&](b2Vec2 center, float hx, float hy) {
        b2BodyDef bd = b2DefaultBodyDef(); bd.position = center;
        b2BodyId body = b2CreateBody(state_->world, &bd);
        b2Polygon poly = b2MakeBox(hx, hy);
        auto sd = shapeDef(0.7f, 0.86f);
        b2CreatePolygonShape(body, &sd, &poly);
    };
    makeWall({15, 0.35f}, 15.0f, 0.35f);
    makeWall({15, 12.65f}, 15.0f, 0.35f);
    makeWall({0.35f, 6.5f}, 0.35f, 6.5f);
    makeWall({29.65f, 6.5f}, 0.35f, 6.5f);
    // A broad bumper creates satisfying bank shots before the scoring bay.
    makeWall({21.7f, 3.2f}, 0.45f, 1.15f);
    makeWall({21.7f, 9.8f}, 0.45f, 1.15f);

    // Staggered, slightly uneven stacks give the pile varied collision paths.
    const b2Vec2 positions[] = {
        {23.8f, 4.0f}, {25.0f, 4.0f}, {26.2f, 4.0f},
        {24.4f, 4.75f}, {25.6f, 4.75f}, {24.1f, 5.5f}, {25.3f, 5.5f},
        {26.3f, 6.25f}, {24.8f, 6.25f}, {23.9f, 7.0f}, {25.1f, 7.0f},
        {24.5f, 7.75f}, {25.7f, 7.75f}, {24.0f, 8.5f}, {25.2f, 8.5f},
    };
    for (int i = 0; i < 15; ++i) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = positions[i];
        bd.rotation = b2MakeRot((i % 3 - 1) * 0.035f);
        b2BodyId body = b2CreateBody(state_->world, &bd);
        b2Polygon poly = b2MakeBox(kTargetSize * 0.5f, kTargetSize * 0.5f);
        auto sd = shapeDef(0.43f, 0.24f);
        sd.density = 1.1f;
        b2CreatePolygonShape(body, &sd, &poly);
        state_->targets.push_back({body, false});
    }
}

void Simulation::setAim(float radians) { state_->aim = std::clamp(radians, -0.85f, 0.85f); }
void Simulation::setCharge(float amount) { state_->charge = std::clamp(amount, 0.15f, 1.0f); }
bool Simulation::fire() {
    if (state_->shots <= 0 || roundOver() || state_->cooldown > 0 || state_->balls.size() >= kMaxBalls) return false;
    const b2Vec2 direction{std::cos(state_->aim), std::sin(state_->aim)};
    const b2Vec2 origin{4.3f, 6.5f};
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {origin.x + direction.x * 0.8f, origin.y + direction.y * 0.8f};
    bd.linearVelocity = {direction.x * (12.0f + state_->charge * 20.0f), direction.y * (12.0f + state_->charge * 20.0f)};
    bd.gravityScale = 0.0f;
    bd.isBullet = true;
    b2BodyId body = b2CreateBody(state_->world, &bd);
    b2Circle circle{{0, 0}, kBallRadius};
    auto sd = shapeDef(0.18f, 0.88f);
    sd.density = 4.0f;
    b2CreateCircleShape(body, &sd, &circle);
    state_->balls.push_back({body, 0});
    --state_->shots;
    state_->cooldown = kCooldownTicks;
    return true;
}

void Simulation::tick() {
    if (state_->cooldown > 0) --state_->cooldown;
    b2World_Step(state_->world, kStep, 4);
    for (auto& ball : state_->balls) ++ball.age;
    state_->balls.erase(std::remove_if(state_->balls.begin(), state_->balls.end(), [&](const Ball& ball) {
        const b2Vec2 p = b2Body_GetPosition(ball.body);
        if (ball.age < 900 && p.x > -1 && p.x < 31 && p.y > -1 && p.y < 14) return false;
        b2DestroyBody(ball.body); return true;
    }), state_->balls.end());

    // The right-hand goal is a scoring lane; targets must settle inside its
    // mouth (x > 27.4) rather than merely being hit.
    for (auto& target : state_->targets) {
        if (target.scored) continue;
        const b2Vec2 p = b2Body_GetPosition(target.body);
        const b2Vec2 v = b2Body_GetLinearVelocity(target.body);
        if (p.x > 27.4f && p.y > 1.7f && p.y < 11.3f && b2Length(v) < 4.0f) {
            target.scored = true;
            state_->score += 100;
        }
    }
}

float Simulation::aim() const { return state_->aim; }
float Simulation::charge() const { return state_->charge; }
int Simulation::shotsLeft() const { return state_->shots; }
int Simulation::score() const { return state_->score; }
int Simulation::targetsScored() const { return static_cast<int>(std::count_if(state_->targets.begin(), state_->targets.end(), [](const Target& t) { return t.scored; })); }
bool Simulation::roundOver() const { return targetsScored() == 15 || (state_->shots == 0 && state_->balls.empty()); }
std::vector<TargetView> Simulation::targets() const {
    std::vector<TargetView> result;
    for (const auto& t : state_->targets) result.push_back({b2Body_GetPosition(t.body), b2Rot_GetAngle(b2Body_GetRotation(t.body)), t.scored});
    return result;
}
std::vector<b2Vec2> Simulation::balls() const {
    std::vector<b2Vec2> result;
    for (const auto& b : state_->balls) result.push_back(b2Body_GetPosition(b.body));
    return result;
}
bool Simulation::ballInPlay() const { return !state_->balls.empty(); }

} // namespace ricochet
