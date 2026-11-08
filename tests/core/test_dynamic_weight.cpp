#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "xieqian/core/draw/draw_engine.h"
#include "xieqian/core/roster/roster.h"

using xieqian::core::DrawEngine;
using xieqian::core::DrawStrategy;
using xieqian::core::dynamicWeight;
using xieqian::core::Participant;
using xieqian::core::participantId;
using xieqian::core::Roster;

namespace {

Participant makeParticipant(const std::string &name, double weight, int drawCount, int streak)
{
    Participant participant;
    participant.name = name;
    participant.weight = weight;
    participant.drawCount = drawCount;
    participant.streak = streak;
    return participant;
}

}  // namespace

TEST_CASE("dynamicWeight applies decay and streak penalty", "[draw][weight]") {
    const Participant fresh = makeParticipant("A", 2.0, 0, 0);
    REQUIRE(dynamicWeight(fresh) == 2.0);

    const Participant drawn = makeParticipant("B", 2.0, 3, 2);
    const double expected = 2.0 * std::exp(-1.0) * 0.6;
    REQUIRE(std::abs(dynamicWeight(drawn) - expected) < 1e-12);

    const Participant repeated = makeParticipant("C", 1.0, 0, 100);
    REQUIRE(std::abs(dynamicWeight(repeated) - 0.1) < 1e-12);

    const Participant disabledWeight = makeParticipant("D", -4.0, 0, 0);
    REQUIRE(dynamicWeight(disabledWeight) == 0.0);
}

TEST_CASE("calculateEffectiveWeight formula for drawCount 6 streak 3", "[draw][weight]") {
    // 该断言覆盖 calculateEffectiveWeight 使用的同一套引擎层数学：
    // base * exp(-6 / 3) * max(0.1, 1 - 3 * 0.2)
    const Participant participant = makeParticipant("E", 2.0, 6, 3);
    const double expected = 2.0 * std::exp(-2.0) * 0.4;
    REQUIRE(std::abs(dynamicWeight(participant) - expected) < 1e-12);
}

TEST_CASE("recordWins bumps winner and resets other streaks", "[roster][weight]") {
    Roster roster;
    roster.add(makeParticipant("A", 1.0, 0, 2));
    roster.add(makeParticipant("B", 1.0, 4, 0));

    roster.recordWins({participantId(roster.at(1))});

    REQUIRE(roster.at(0).drawCount == 0);
    REQUIRE(roster.at(0).streak == 0);
    REQUIRE(roster.at(1).drawCount == 5);
    REQUIRE(roster.at(1).streak == 1);

    roster.resetDrawStats();
    REQUIRE(roster.at(0).drawCount == 0);
    REQUIRE(roster.at(1).drawCount == 0);
    REQUIRE(roster.at(1).streak == 0);
}

TEST_CASE("dynamic mode penalises frequently drawn participants", "[draw][weight]") {
    std::vector<Participant> pool;
    pool.push_back(makeParticipant("hot", 1.0, 12, 1));
    pool.push_back(makeParticipant("cold", 1.0, 0, 0));

    DrawEngine engine(std::mt19937_64(2024));
    engine.setDynamicWeights(true);
    REQUIRE(engine.dynamicWeights());

    std::size_t hotWins = 0;
    constexpr int rounds = 400;
    for (int round = 0; round < rounds; ++round) {
        const auto winners = engine.draw(pool, 1, DrawStrategy::Weighted);
        REQUIRE(winners.size() == 1);
        if (winners.front().name == "hot") {
            ++hotWins;
        }
    }

    REQUIRE(hotWins * 4 < static_cast<std::size_t>(rounds));
}

TEST_CASE("explicit effective weights override participant weights", "[draw][weight]") {
    std::vector<Participant> pool;
    pool.push_back(makeParticipant("A", 100.0, 0, 0));
    pool.push_back(makeParticipant("B", 100.0, 0, 0));
    pool.push_back(makeParticipant("C", 0.0, 0, 0));

    DrawEngine engine(std::mt19937_64(11));
    const std::vector<double> weights{0.0, 0.0, 1.0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto winners = engine.drawWithWeights(pool, 1, weights);
        REQUIRE(winners.size() == 1);
        REQUIRE(winners.front().name == "C");
    }
}

TEST_CASE("static mode keeps weights unchanged", "[draw][weight]") {
    std::vector<Participant> pool;
    pool.push_back(makeParticipant("hot", 1.0, 100, 5));
    pool.push_back(makeParticipant("cold", 1.0, 0, 0));

    DrawEngine engine(std::mt19937_64(7));
    engine.setDynamicWeights(false);

    std::size_t hotWins = 0;
    constexpr int rounds = 400;
    for (int round = 0; round < rounds; ++round) {
        const auto winners = engine.draw(pool, 1, DrawStrategy::Weighted);
        if (winners.front().name == "hot") {
            ++hotWins;
        }
    }

    REQUIRE(hotWins > static_cast<std::size_t>(rounds) / 4);
    REQUIRE(hotWins < static_cast<std::size_t>(rounds) * 3 / 4);
}
