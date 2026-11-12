#include "charge_effect.h"
#include "wall_dive.h"
#include "air_combo.h"
#include "aerial_effect.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    assert(ReadChargeFrame(0x19e62a, 1).move == ChargeMove::ET);
    assert(ReadChargeFrame(0x19e73e, 22).move == ChargeMove::ET);
    assert(ReadChargeFrame(0x19e780, 71).move == ChargeMove::ET);
    assert(ReadChargeFrame(0x19e8a0, 1).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x19e9f8, 71).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x19e394, 30).move == ChargeMove::None);
    assert(ReadChargeFrame(0x19e824, 72).move == ChargeMove::None);
    assert(ReadChargeFrame(0x19521e, 1).move == ChargeMove::None);
    assert(ReadChargeFrame(0x1b6412, 1).family == ChargeFamily::Chain);
    assert(ReadChargeFrame(0x1b6636, 1).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x1ba83a, 4).move == ChargeMove::None);
    assert(ReadChargeFrame(0x1ba83a, 5).move == ChargeMove::ET);
    assert(ReadChargeFrame(0x1ba83a, 15).frame == 11);
    assert(ReadChargeFrame(0x1baa58, 16).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x1baa8e, 26).frame == 11);
    assert(ReadChargeFrame(0x1ba08e, 1).move == ChargeMove::None);
    assert(ReadChargeFrame(0x1bb770, 1).move == ChargeMove::ET);
    assert(ReadChargeFrame(0x1bb940, 1).move == ChargeMove::UT);
    for (const auto script : {0x1bb764u, 0x1bb94au}) {
        ChargeTimeline dabilahro;
        assert(dabilahro.Advance(ReadChargeFrame(script, 1), false, 1));
        assert(!dabilahro.CapturesOrigin() && !dabilahro.SamplesTrail());
        assert(dabilahro.BodyOpacity(1, 9, 12) == 1);
        dabilahro.Advance(ReadChargeFrame(script, 10), false, 10);
        assert(!dabilahro.SamplesTrail() && dabilahro.BodyOpacity(10, 9, 12) == 0.4f);
        dabilahro.Advance(ReadChargeFrame(script, 11), false, 11);
        assert(!dabilahro.CapturesOrigin() && dabilahro.SamplesTrail() && dabilahro.BodyOpacity(11, 9, 12) == 0);
        dabilahro.Advance(ReadChargeFrame(script, 35), false, 35);
        assert(!dabilahro.SamplesTrail() && dabilahro.BodyOpacity(35, 9, 12) == 1);
    }
    ChargeTimeline darkDragonBlade;
    assert(darkDragonBlade.Advance(ReadChargeFrame(0x1ba83a, 5), false, 5));
    for (unsigned frame = 5; frame < 73; ++frame) {
        darkDragonBlade.Advance(ReadChargeFrame(0x1ba83a, float(frame)), false, frame);
        assert(!darkDragonBlade.CapturesOrigin() && !darkDragonBlade.SamplesTrail());
        assert(darkDragonBlade.BodyOpacity(frame, 9, 12) == 1);
    }
    assert(darkDragonBlade.Advance(ReadChargeFrame(0x1baa58, 16), false, 120));
    assert(darkDragonBlade.CapturesOrigin() && darkDragonBlade.BodyOpacity(120, 9, 12) == 0.4f);
    darkDragonBlade.Advance(ReadChargeFrame(0x1baa8e, 26), false, 130);
    assert(darkDragonBlade.SamplesTrail() && darkDragonBlade.BodyOpacity(130, 9, 12) == 0);
    assert(ReadChargeFrame(0x1ba248, 11).visual == ChargeVisual::AirTrail);
    assert(ReadChargeFrame(0x1ba450, 11).visual == ChargeVisual::AirTrail);
    assert(ReadChargeFrame(0x1bb0cc, 6).move == ChargeMove::None);
    assert(ReadChargeFrame(0x1bb0cc, 17).frame == 11);
    assert(ReadChargeFrame(0x1bb0cc, 17).visual == ChargeVisual::AirTrail);
    for (auto script : {0x1b037a, 0x1b071c}) {
        ChargeTimeline spin;
        assert(spin.Advance(ReadChargeFrame(script, 1), false, 1));
        spin.Advance(ReadChargeFrame(script, 11), false, 11);
        assert(!spin.CapturesOrigin() && !spin.SamplesTrail() && spin.BodyOpacity(11, 9, 12) == 1);
    }
    assert(ReadChargeFrame(0x1c054e, 1).family == ChargeFamily::Lunar);
    assert(ReadChargeFrame(0x1c07f2, 1).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x1c897a, 1).family == ChargeFamily::Dual);
    assert(ReadChargeFrame(0x1c8b88, 2).move == ChargeMove::UT);
    assert(ReadChargeFrame(0x1c8780, 1).move == ChargeMove::None);
    assert(ChargeFollowThrough(0x19c716) && ChargeFollowThrough(0x1aea7e));
    assert(ChargeFollowThrough(0x1b5b6c) && !ChargeFollowThrough(0x19521e));
    assert(ChargeBodyAlpha(ChargeMove::ET, 0, 9) == 0.4f);
    assert(ChargeBodyAlpha(ChargeMove::ET, 1, 9) == 0);
    assert(ChargeBodyAlpha(ChargeMove::ET, 19, 9) == 0);
    assert(ChargeBodyAlpha(ChargeMove::ET, 20, 9) > 0);
    assert(ChargeBodyAlpha(ChargeMove::ET, 28, 9) == 1);
    assert(ChargeBodyAlpha(ChargeMove::UT, 22, 12) == 0);
    assert(ChargeBodyAlpha(ChargeMove::UT, 23, 12) > 0);
    assert(ChargeBodyAlpha(ChargeMove::UT, 34, 12) == 1);
    assert(AnchorAlpha(0) == 1 && AnchorAlpha(ChargeTravelAge) == 1);
    assert(AnchorAlpha(40) < AnchorAlpha(39) && AnchorAlpha(40) > AnchorAlpha(41));
    assert(AnchorAlpha(62) > 0 && AnchorAlpha(63) == 0);
    ChargeTimeline timeline;
    assert(timeline.Advance({ChargeMove::ET, 2}, false, 1));
    assert(timeline.Age() == 0 && timeline.BodyOpacity(1, 9, 12) == 0.4f);
    timeline.Advance({ChargeMove::ET, 21}, false, 20);
    assert(timeline.Age() == 19 && timeline.BodyOpacity(20, 9, 12) == 0);
    timeline.Advance({ChargeMove::ET, 22}, false, 21);
    assert(timeline.BodyOpacity(21, 9, 12) > 0);
    timeline.Advance({ChargeMove::ET, 55}, false, 54);
    assert(!timeline.Advance({ChargeMove::ET, 1}, false, 55));
    assert(!timeline.Advance({ChargeMove::ET, 2}, false, 56));
    assert(timeline.BodyOpacity(56, 9, 12) == 1);
    timeline.Advance({}, false, 57);
    assert(timeline.Advance({ChargeMove::UT, 1}, false, 58));
    timeline.Advance({ChargeMove::UT, 26}, false, 83);
    const float beforeChain = timeline.BodyOpacity(83, 9, 12);
    timeline.Advance({}, true, 84);
    const float chainOpacity = timeline.BodyOpacity(84, 9, 12);
    assert(chainOpacity > beforeChain && chainOpacity < 1);
    assert(timeline.BodyOpacity(92, 9, 12) == 1);
    timeline.Advance({}, false, 93);
    assert(timeline.BodyOpacity(93, 9, 12) == 1);
    assert(timeline.Advance(ReadChargeFrame(0x1c897a, 1), false, 94));
    timeline.Advance(ReadChargeFrame(0x1c8a40, 21), false, 114);
    const float beforeDualContact = timeline.BodyOpacity(114, 9, 12);
    timeline.Advance({}, ChargeFollowThrough(0x1cab86), 115);
    const float dualContactOpacity = timeline.BodyOpacity(115, 9, 12);
    assert(dualContactOpacity > beforeDualContact && dualContactOpacity < 1);
    assert(timeline.BodyOpacity(121, 9, 12) < 1 && timeline.BodyOpacity(122, 9, 12) == 1);

    assert(WindRunBodyAlpha(5) == 1 && WindRunBodyAlpha(6) == 0.4f);
    assert(WindRunBodyAlpha(7) == 0 && WindRunBodyAlpha(13) == 0);
    assert(WindRunLastTrailFrame < WindRunApexFrame);
    assert(WindRunBodyAlpha(14) > 0 && WindRunBodyAlpha(22) == 1);
    WallDiveTimeline wall;
    constexpr unsigned landingScripts[]{0x1a1bb6, 0x1b57c8, 0x1b99da, 0x1bfb26, 0x1c8302, 0x1cbe26};
    for (const auto& route : WallDiveRoutes) {
        const auto landing = landingScripts[unsigned(route.family)];
        wall.Advance(landing, 2, 100);
        assert(!wall.diving && !wall.landing && wall.BodyOpacity(100) == 1);
        wall.Advance(route.begin + 12, 20, 101);
        assert(!wall.diving && wall.BodyOpacity(101) == 1);
        wall.Advance(route.begin + 64, 26, 102);
        assert(wall.diving && wall.route == &route && wall.BodyOpacity(102) == 0.4f);
        wall.Advance(route.end - 4, 34, 103);
        assert(wall.diving && wall.BodyOpacity(103) == 0);
        wall.Advance(route.end, 37, 106);
        assert(wall.diving);
        wall.Advance(landing, 2, 108);
        assert(!wall.diving && wall.landing == 108 && wall.BodyOpacity(115) == 0);
        wall.Advance(landing + 4, 10, 116);
        assert(wall.BodyOpacity(116) > 0 && wall.BodyOpacity(122) < 1 && wall.BodyOpacity(123) == 1);
        wall.Advance(0x19521e, 1, 124);
        assert(!wall.diving && !wall.landing && wall.BodyOpacity(124) == 1);
    }
    wall.Advance(0x1bf85e, 26, 130);
    wall.Advance(0x1bfac6, 1, 131);
    assert(wall.diving && !wall.landing);
    wall.Advance(0x1bfb26, 1, 132);
    assert(!wall.diving && wall.landing == 132);
    wall.Advance(0x1aee9c, 30, 133);
    assert(!wall.diving && !wall.landing);
    wall.Advance(0x19f68e, 8, 134);
    assert(!wall.diving && !wall.landing);
    SwallowTimeline flying;
    flying.Advance(0x1a445e, 14, 1);
    assert(!flying.route && flying.BodyOpacity(1) == 1);
    flying.Advance(0x19f876, 1, 2);
    assert(flying.route == 494 && flying.sample && flying.BodyOpacity(2) == 0.4f);
    flying.Advance(0x19f88a, 2, 3);
    assert(flying.sample && flying.BodyOpacity(3) == 0);
    flying.Advance(0x19f88a, 2, 4);
    assert(!flying.sample && flying.pauseFrames == 1);
    flying.Advance(0x19f810, 1, 5);
    assert(!flying.route && !flying.sample && flying.BodyOpacity(5) > 0 && flying.BodyOpacity(5) < 1);
    flying.Advance(0x19f810, 6, 10);
    assert(flying.BodyOpacity(10) == 1 && !flying.reveal);
    flying.Advance(0x19f62e, 1, 11);
    assert(flying.route == 490 && flying.sample);
    flying.Advance(0x19f6f2, 14, 12);
    assert(flying.sample && flying.BodyOpacity(12) == 0);
    flying.Advance(0x19f70a, 1, 13);
    assert(!flying.sample && flying.BodyOpacity(13) < 1);
    assert(!WhiteFeatherFrame(0x19c99a, 112).frame);
    assert(WhiteFeatherFrame(0x19c99a, 113).BodyOpacity() == 0.4f);
    assert(WhiteFeatherFrame(0x19ca82, 124).BodyOpacity() == 0);
    assert(WhiteFeatherFrame(0x19cad4, 125).impact == 125);
    assert(WhiteFeatherFrame(0x19caf0, 126).BodyOpacity() == 0.125f);
    assert(WhiteFeatherFrame(0x19caf0, 133).BodyOpacity() == 1);
    assert(WhiteFeatherFrame(0x1ae764, 127).impact == 127);
    assert(!WhiteFeatherFrame(0x1aee9c, 125).frame);
    AirCombo skipped;
    skipped.Advance(0x1aeb9c, 59, 1);
    assert(skipped.Advance(0x1aec06, 1, 2));
    skipped.Advance(0x1aec80, 9, 9);
    assert(skipped.trailCapture && skipped.samples == 1);
    skipped.Advance(0x1aec80, 9, 10);
    assert(!skipped.trailCapture && skipped.samples == 1);
    skipped.Advance(0x1aec90, 11, 11);
    assert(skipped.trailCapture && skipped.samples == 2);
    skipped.Advance(0x1aec90, 12, 12);
    assert(skipped.trailCapture && skipped.samples == 3);
    skipped.Advance(0x1aec90, 13, 13);
    assert(!skipped.trailCapture && skipped.samples == 3);
    if (argc > 1) {
        std::ifstream input(argv[1]);
        std::string line;
        assert(std::getline(input, line));
        AirCombo air;
        unsigned starts = 0, descents = 0, descentFrames = 0, layers = 0;
        uint64_t sample = 0;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            std::string timestamp, pc, unused, frame;
            if (!(std::getline(row, timestamp, ',') && std::getline(row, pc, ',') &&
                std::getline(row, unused, ',') && std::getline(row, frame, ','))) continue;
            const bool wasDescending = air.descending;
            const auto motion = std::stof(frame);
            if (air.Advance(std::stoul(pc, nullptr, 16), motion, ++sample)) ++starts;
            if (air.trailCapture) {
                assert(motion >= AirFirstTrailFrame && motion < AirFirstTrailFrame + AirTrailCount);
                ++layers;
            }
            if (air.descending && !wasDescending) ++descents;
            if (air.descentCapture) {
                assert(motion >= DescentStartFrame && motion < DescentLandingFrame);
                ++descentFrames;
            }
        }
        assert(starts >= 18 && descents == 2 && layers == starts * 3 && descentFrames >= 24);
        std::printf("True Dragon UT trace: %u strikes, %u moving poses, %u late-descent frames.\n",
            starts, layers, descentFrames);
    }
    if (argc > 2) {
        std::ifstream input(argv[2]);
        std::string line;
        assert(std::getline(input, line));
        ChargeTimeline releases;
        SwallowTimeline swallows;
        std::array<std::array<unsigned, 2>, 6> counts{};
        unsigned normalSwallows = 0, reverseSwallows = 0, featherImpacts = 0;
        uint64_t sample = 0;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            std::string timestamp, pc, unused, frame;
            if (!(std::getline(row, timestamp, ',') && std::getline(row, pc, ',') &&
                std::getline(row, unused, ',') && std::getline(row, frame, ','))) continue;
            ++sample;
            const auto script = std::stoul(pc, nullptr, 16);
            const auto motion = std::stof(frame);
            const auto release = ReadChargeFrame(script, motion);
            if (releases.Advance(release, ChargeFollowThrough(script), sample))
                ++counts[unsigned(release.family)][release.move == ChargeMove::UT];
            const auto oldRoute = swallows.route;
            swallows.Advance(script, motion, sample);
            if (!oldRoute && swallows.route == 490) ++normalSwallows;
            if (!oldRoute && swallows.route == 494) ++reverseSwallows;
            const auto feather = WhiteFeatherFrame(script, motion);
            if (feather.frame && feather.frame == feather.impact) ++featherImpacts;
        }
        for (auto family : {ChargeFamily::Chain, ChargeFamily::Lunar, ChargeFamily::Heavy})
            assert(counts[unsigned(family)][0] && counts[unsigned(family)][1]);
        assert(counts[unsigned(ChargeFamily::Dragon)][1] && normalSwallows >= 2 && reverseSwallows >= 2 && featherImpacts);
        std::printf("Live trace replay: sword swallows %u + %u, feather impact samples %u, chain/staff/heavy ET and UT passed.\n",
            normalSwallows, reverseSwallows, featherImpacts);
    }
    std::puts("ET, UT, wind run, and wall dive timing and body fade checks passed.");
}
