#include "Sim/AIDifficulty.h"
#include "Sim/Simulation.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x-b.x, a.y-b.y); }
void advance(Simulation& sim, float seconds) {
    for (int i=0; i<static_cast<int>(std::ceil(seconds/Simulation::Step)); ++i) sim.update(Simulation::Step);
}
Simulation fixture(AIDifficulty level, float seconds=500) {
    Simulation sim; sim.reset({0,7300,true,aiDifficultyAggression(level)});
    const_cast<std::vector<Entity>&>(sim.entities()).clear();
    const_cast<std::vector<Obstacle>&>(sim.obstacles()).clear();
    sim.debugSpawn(Kind::Headquarters,0,{500,500});
    sim.debugSpawn(Kind::Headquarters,1,{4200,4200});
    sim.debugResources(1,0); advance(sim,seconds); return sim;
}
std::vector<Id> units(Simulation& sim, Kind kind, int n, Vec2 point, int team=1) {
    std::vector<Id> result;
    for (int i=0;i<n;++i) result.push_back(sim.debugSpawn(kind,team,{point.x+i%4*(kind==Kind::Resource?140:55),point.y+i/4*55}));
    return result;
}
void infrastructure(Simulation& sim) {
    for (int i=0;i<8;++i) sim.debugSpawn(Kind::Foundry,1,{3400.0f+i%4*220,3500.0f+i/4*240});
    for (int i=0;i<5;++i) sim.debugSpawn(Kind::Processor,1,{3350.0f+i*240,4600});
    sim.debugSpawn(Kind::Laboratory,1,{4600,3950});
    sim.debugSpawn(Kind::Turret,1,{4450,4200});
}
void capturedOreBecomesPaidIncomeAndDefense() {
    for (auto level : {AIDifficulty::Hard,AIDifficulty::Expert}) {
        auto sim=fixture(level); infrastructure(sim);
        sim.debugSpawn(Kind::Headquarters,0,{4500,500}); // Keep the match alive through construction and mining.
        const Vec2 patch{1600,2200}; // Deliberately outside every authored expansion landmark.
        const auto ore=units(sim,Kind::Resource,4,patch,-1);
        const Id capturedBase=sim.debugSpawn(Kind::Headquarters,0,{1820,2040});
        const_cast<Entity*>(sim.find(capturedBase))->hp=1; // End of a won battle; normal weapons finish it.
        const auto army=units(sim,Kind::Striker,16,{1920,2140});
        units(sim,Kind::Worker,6,{1780,2450});
        units(sim,Kind::Worker,10,{4100,4450});
        Command attack; attack.type=CommandType::Attack; attack.team=1; attack.units=army; attack.target=capturedBase;
        check(sim.command(attack).accepted,"capture fixture could not issue a normal attack");
        advance(sim,2.1f);
        check(!sim.find(capturedBase) || !sim.find(capturedBase)->alive(),"capture fixture did not clear enemy base");
        sim.debugResources(1,2500);
        const int gathered=sim.players()[1].stats.gathered;
        const auto first=sim.recording().size(); advance(sim,175);
        bool purchased=false, operational=false, guarded=false;
        for (std::size_t i=first;i<sim.recording().size();++i) {
            const Command& order=sim.recording()[i].command;
            purchased |= order.team==1 && order.type==CommandType::Build && order.kind==Kind::Headquarters && distance(order.point,patch)<850;
        }
        for (const auto& entity:sim.entities()) if (entity.alive() && entity.team==1 && distance(entity.pos,patch)<950) {
            operational |= entity.kind==Kind::Headquarters && entity.progress>=1;
            guarded |= entity.kind==Kind::Turret && entity.progress>=1;
        }
        int miners=0;
        for (const auto& entity:sim.entities()) if (entity.alive() && entity.team==1 && entity.kind==Kind::Worker &&
            std::find(ore.begin(),ore.end(),entity.resourceTarget)!=ore.end()) ++miners;
        std::cout<<"EVIDENCE "<<aiDifficultyName(level)<<" reclaimed_hq="<<operational<<" ward="<<guarded
                 <<" miners="<<miners<<" income="<<sim.players()[1].stats.gathered-gathered<<'\n';
        check(purchased && operational,"AI did not buy and finish a base at captured off-landmark ore");
        check(miners>=3 && sim.players()[1].stats.gathered>gathered+100,"captured ore did not become delivered income");
        check(guarded,"captured mining line was left without a completed Ward");
    }
}
void guardsPreserveMainArmyAndSave() {
    for (auto level:{AIDifficulty::Hard,AIDifficulty::Expert}) {
        auto sim=fixture(level,200);
        sim.debugSpawn(Kind::Headquarters,1,{1800,2600});
        sim.debugSpawn(Kind::Resource,-1,{1550,2600});
        const auto locals=units(sim,Kind::Striker,5,{1630,2530});
        units(sim,Kind::Striker,15,{3500,3550});
        const auto first=sim.recording().size(); advance(sim,2.1f);
        int guards=0; bool main=false;
        for (Id id:locals) guards += sim.find(id)->order==Order::Hold;
        for (std::size_t i=first;i<sim.recording().size();++i) {
            const auto& order=sim.recording()[i].command;
            main |= order.type==CommandType::AttackMove && order.team==1 && order.units.size()>=11;
        }
        check(guards==(level==AIDifficulty::Expert?3:2),"productive frontier did not retain its bounded guard");
        check(main,"ore guards stopped the main army from launching");
        const auto path=std::filesystem::temp_directory_path()/"cinder-ore-guards.save";
        check(sim.save(path.string()),"could not save ore guards");
        Simulation restored; check(restored.load(path.string()),"could not load ore guards"); std::filesystem::remove(path);
        for (int i=0;i<200;++i) { sim.update(Simulation::Step); restored.update(Simulation::Step);
            check(sim.stateHash()==restored.stateHash(),"ore guard orders diverged after save/load"); }
        const Id raider=sim.debugSpawn(Kind::Striker,0,{1510,2470});
        const float hp=sim.find(raider)->hp; advance(sim,6);
        check(!sim.find(raider) || sim.find(raider)->hp<hp,"standing ore guards did not engage a counterraid");
    }
}
bool raidsMiners(bool visible, bool guarded) {
    auto sim=fixture(AIDifficulty::Expert,200);
    units(sim,Kind::Striker,20,{2300,1500});
    sim.debugSpawn(Kind::Scout,1,{1000,900});
    const Vec2 line{1100,3500}; units(sim,Kind::Worker,3,line,0);
    if (visible) sim.debugSpawn(Kind::Scout,1,{1400,3200});
    if (guarded) sim.debugSpawn(Kind::Turret,0,{1350,3550});
    check(sim.visible(1,line)==visible,"miner raid visibility fixture incorrect");
    const auto first=sim.recording().size(); advance(sim,2.1f);
    for (std::size_t i=first;i<sim.recording().size();++i) {
        const auto& order=sim.recording()[i].command;
        if (order.team==1 && order.type==CommandType::AttackMove && order.units.size()==3 && distance(order.point,line)<300) return true;
    }
    return false;
}
void expertDeniesExposedIncome() {
    check(raidsMiners(true,false),"Expert ignored exposed miners outside a Headquarters");
    check(!raidsMiners(false,false),"Expert targeted hidden miners");
    check(!raidsMiners(true,true),"Expert sent a small worker raid into a known Ward");
}
void expertFundsAThirdProductiveBase() {
    for (auto level:{AIDifficulty::Hard,AIDifficulty::Expert}) {
        auto sim=fixture(level); infrastructure(sim);
        sim.debugSpawn(Kind::Resource,-1,{4400,4400});
        sim.debugSpawn(Kind::Headquarters,1,{2900,3800});
        sim.debugSpawn(Kind::Resource,-1,{2750,3950});
        sim.debugSpawn(Kind::Turret,1,{3100,3800});
        const Vec2 third{1700,2000}; sim.debugSpawn(Kind::Resource,-1,third);
        units(sim,Kind::Worker,24,{4000,4300});
        sim.debugSpawn(Kind::Worker,1,{1800,2220});
        sim.debugResources(1,2500);
        const auto first=sim.recording().size(); advance(sim,8.1f);
        bool boughtThird=false; int workers=0;
        for (std::size_t i=first;i<sim.recording().size();++i) {
            const auto& c=sim.recording()[i].command;
            boughtThird |= c.team==1 && c.type==CommandType::Build && c.kind==Kind::Headquarters && distance(c.point,third)<850;
        }
        for (const auto& e:sim.entities()) if(e.alive() && e.team==1) {
            workers+=e.kind==Kind::Worker;
            for(const auto& q:e.queue) workers+=!q.research && q.kind==Kind::Worker;
        }
        check(boughtThird==(level==AIDifficulty::Expert),"productive base targets did not distinguish Hard and Expert");
        if(level==AIDifficulty::Expert) check(workers>=27,"Expert did not fund workers for its third productive base");
    }
}
void unseenOreDoesNotChangeOrders() {
    for (auto level:{AIDifficulty::Hard,AIDifficulty::Expert}) {
        auto a=fixture(level), b=fixture(level); infrastructure(a); infrastructure(b);
        units(a,Kind::Worker,14,{4000,4350}); units(b,Kind::Worker,14,{4000,4350});
        const Id visibleOnlyToPlayer=a.debugSpawn(Kind::Resource,-1,{700,2200});
        b.debugSpawn(Kind::Resource,-1,{700,2200});
        const_cast<Entity*>(b.find(visibleOnlyToPlayer))->resource=0;
        check(!a.visible(1,{700,2200}),"ore isolation fixture became visible");
        a.debugResources(1,2000); b.debugResources(1,2000);
        const auto first=a.recording().size(); advance(a,2.1f); advance(b,2.1f);
        check(a.recording().size()==b.recording().size(),"hidden ore changed command count");
        for (std::size_t i=first;i<a.recording().size();++i) {
            const auto& x=a.recording()[i].command; const auto& y=b.recording()[i].command;
            check(x.type==y.type && x.units==y.units && x.kind==y.kind && x.target==y.target && distance(x.point,y.point)<0.01f,
                  "hidden ore changed an AI decision");
        }
    }
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"captured ore becomes income and defense",capturedOreBecomesPaidIncomeAndDefense},
        {"bounded guards preserve main army and saves",guardsPreserveMainArmyAndSave},
        {"Expert denies exposed income",expertDeniesExposedIncome},
        {"Expert third base and workforce",expertFundsAThirdProductiveBase},
        {"hidden ore isolation",unseenOreDoesNotChangeOrders}};
    int failures=0;
    for (const auto& test:tests) try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error) {++failures;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    return failures?1:0;
}
