#include "Sim/Simulation.h"
#include "Sim/SustainedOrderRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace cinder {
namespace {
float tacticsDistanceSquared(Vec2 left,Vec2 right) {
    const float x=left.x-right.x,y=left.y-right.y;return x*x+y*y;
}
bool tacticsFinite(Vec2 point) { return std::isfinite(point.x)&&std::isfinite(point.y); }
bool tacticsZero(Vec2 point) { return point.x==0.0f&&point.y==0.0f; }
bool tacticsMobile(const Entity& entity) {
    return entity.alive()&&!definition(entity.kind).building&&entity.kind!=Kind::Resource;
}
bool tacticsEmpty(const SustainedOrderState& state) {
    return tacticsZero(state.patrolOrigin)&&tacticsZero(state.patrolDestination)&&
        !state.patrolTowardDestination&&!state.escortTarget&&tacticsZero(state.escortOffset)&&
        !state.pursuitTarget&&tacticsZero(state.pursuitAnchor)&&
        state.phase==SustainedOrderPhase::Travel;
}
}

void Simulation::clearSustainedOrder(Entity& entity) { entity.sustained={}; }

Vec2 Simulation::escortFollowPoint(const Entity& escort,const Entity& leader) const {
    return rules::projectEscortFollowPoint(leader.pos,definition(leader.kind).radius,
        definition(escort.kind).radius,escort.sustained.escortOffset,worldSize(),EscortSpacing);
}

bool Simulation::validateSustainedState(const std::vector<Entity>& entities) const {
    std::unordered_map<Id,const Entity*> byId;
    byId.reserve(entities.size());
    for(const auto& entity:entities)if(!entity.id||!byId.emplace(entity.id,&entity).second)return false;
    auto inWorld=[&](Vec2 point) {
        return tacticsFinite(point)&&point.x>=0&&point.y>=0&&point.x<=worldSize()&&point.y<=worldSize();
    };
    auto validEnemy=[&](const Entity& owner,Id id) {
        const auto found=byId.find(id);if(found==byId.end())return false;
        const Entity& enemy=*found->second;
        return enemy.alive()&&enemy.team>=0&&enemy.team!=owner.team&&enemy.kind!=Kind::Resource&&
            (!definition(enemy.kind).air||definition(owner.kind).antiAir)&&definition(owner.kind).damage>0;
    };
    for(const auto& entity:entities) {
        const auto& state=entity.sustained;
        const int phase=static_cast<int>(state.phase);
        if(phase<static_cast<int>(SustainedOrderPhase::Travel)||phase>static_cast<int>(SustainedOrderPhase::Return))return false;
        if(entity.order!=Order::Patrol&&entity.order!=Order::Escort) {
            if(!tacticsEmpty(state))return false;
            continue;
        }
        if(!tacticsMobile(entity)||entity.team<0||entity.target||entity.supportTarget)return false;
        if(entity.order==Order::Patrol) {
            if(!inWorld(state.patrolOrigin)||!inWorld(state.patrolDestination)||state.escortTarget||
               !tacticsZero(state.escortOffset))return false;
            const Vec2 expected=state.patrolTowardDestination?state.patrolDestination:state.patrolOrigin;
            if(entity.goal.x!=expected.x||entity.goal.y!=expected.y)return false;
            if(state.phase==SustainedOrderPhase::Travel) {
                if(state.pursuitTarget||!tacticsZero(state.pursuitAnchor))return false;
            } else if(state.phase==SustainedOrderPhase::Pursuit) {
                if(!state.pursuitTarget||!inWorld(state.pursuitAnchor)||!validEnemy(entity,state.pursuitTarget))return false;
                const Entity& enemy=*byId.find(state.pursuitTarget)->second;
                const float leashSquared=SustainedPursuitRadius*SustainedPursuitRadius;
                if(tacticsDistanceSquared(entity.pos,state.pursuitAnchor)>leashSquared||
                   tacticsDistanceSquared(enemy.pos,state.pursuitAnchor)>leashSquared||
                   tacticsDistanceSquared(entity.pos,enemy.pos)>definition(entity.kind).vision*definition(entity.kind).vision)return false;
            } else if(state.pursuitTarget||!inWorld(state.pursuitAnchor))return false;
        } else {
            if(!tacticsZero(state.patrolOrigin)||!tacticsZero(state.patrolDestination)||state.patrolTowardDestination||
               !tacticsZero(state.pursuitAnchor)||!state.escortTarget||!tacticsFinite(state.escortOffset)||
               tacticsDistanceSquared(state.escortOffset,{})>MaxEscortOffset*MaxEscortOffset)return false;
            const auto found=byId.find(state.escortTarget);
            if(found==byId.end())return false;
            const Entity& leader=*found->second;
            if(!tacticsMobile(leader)||leader.team!=entity.team||leader.id==entity.id)return false;
            const float clearance=definition(entity.kind).radius+definition(leader.kind).radius;
            if(tacticsDistanceSquared(state.escortOffset,{})<clearance*clearance)return false;
            const Vec2 expected=escortFollowPoint(entity,leader);
            if(entity.goal.x!=expected.x||entity.goal.y!=expected.y)return false;
            if(state.phase==SustainedOrderPhase::Travel||state.phase==SustainedOrderPhase::Return) {
                if(state.pursuitTarget)return false;
            } else {
                if(!state.pursuitTarget||!validEnemy(entity,state.pursuitTarget))return false;
                const Entity& enemy=*byId.find(state.pursuitTarget)->second;
                const float leashSquared=SustainedPursuitRadius*SustainedPursuitRadius;
                if(tacticsDistanceSquared(entity.pos,expected)>leashSquared||
                   tacticsDistanceSquared(enemy.pos,expected)>leashSquared||
                   tacticsDistanceSquared(entity.pos,enemy.pos)>definition(entity.kind).vision*definition(entity.kind).vision)return false;
            }
        }
    }
    // Every escort edge must terminate without returning to an already visited
    // escort. This permits chains but rejects direct and indirect cycles.
    for(const auto& entity:entities)if(entity.order==Order::Escort) {
        std::unordered_set<Id> visited;
        const Entity* cursor=&entity;
        while(cursor&&cursor->order==Order::Escort) {
            if(!visited.insert(cursor->id).second)return false;
            const auto found=byId.find(cursor->sustained.escortTarget);
            cursor=found==byId.end()?nullptr:found->second;
        }
    }
    // Separate commands share one leader formation too. Reject imported or
    // persisted states whose derived slots overlap, regardless of whether the
    // immutable nominal offsets differ.
    for(std::size_t left=0;left<entities.size();++left) {
        const Entity& first=entities[left];
        if(first.order!=Order::Escort)continue;
        for(std::size_t right=left+1;right<entities.size();++right) {
            const Entity& second=entities[right];
            if(second.order!=Order::Escort||
               second.sustained.escortTarget!=first.sustained.escortTarget)continue;
            const float clearance=definition(first.kind).radius+definition(second.kind).radius;
            if(tacticsDistanceSquared(first.goal,second.goal)<clearance*clearance)return false;
        }
    }
    return true;
}

bool Simulation::refreshSustainedOrder(Entity& entity) {
    if(entity.order!=Order::Patrol&&entity.order!=Order::Escort)return false;
    auto& state=entity.sustained;
    Vec2 leashCenter{};
    if(entity.order==Order::Escort) {
        const Entity* leader=find(state.escortTarget);
        if(!leader||!leader->alive()||leader->team!=entity.team||leader->id==entity.id||
           definition(leader->kind).building||leader->kind==Kind::Resource) {
            const Vec2 lastFollow=entity.goal;
            resetCurrentOrder(entity,true);
            if(!activateNextOrder(entity)) {entity.order=Order::Defend;entity.goal=lastFollow;}
            return false;
        }
        const Vec2 previousGoal=entity.goal;
        entity.goal=escortFollowPoint(entity,*leader);
        if(entity.navigationExhausted&&tacticsDistanceSquared(previousGoal,entity.goal)>4.0f*4.0f&&
           (tick_+entity.id)%10==0)resetNavigation(entity);
        leashCenter=entity.goal;
    } else {
        leashCenter=state.phase==SustainedOrderPhase::Travel?entity.pos:
            state.phase==SustainedOrderPhase::Pursuit?state.pursuitAnchor:entity.goal;
    }

    auto eligible=[&](const Entity* enemy,Vec2 center) {
        if(!enemy||!enemy->alive()||enemy->team<0||enemy->team==entity.team||enemy->kind==Kind::Resource||
           (definition(enemy->kind).air&&!definition(entity.kind).antiAir)||!visible(entity.team,enemy->pos))return false;
        const float leashSquared=SustainedPursuitRadius*SustainedPursuitRadius;
        return tacticsDistanceSquared(entity.pos,center)<=leashSquared&&
            tacticsDistanceSquared(enemy->pos,center)<=leashSquared&&
            tacticsDistanceSquared(entity.pos,enemy->pos)<=definition(entity.kind).vision*definition(entity.kind).vision;
    };

    if(state.phase==SustainedOrderPhase::Pursuit) {
        if(!eligible(find(state.pursuitTarget),leashCenter)) {
            state.pursuitTarget=0;state.phase=SustainedOrderPhase::Return;resetNavigation(entity);
        } else return true;
    }
    if(state.phase==SustainedOrderPhase::Return) {
        // Return still completes the immutable active endpoint, but uses the
        // same practical arrival radius as ordinary tactical movement. Requiring
        // center-perfect contact can deadlock while attacks are suppressed when
        // an enemy or friendly unit occupies the endpoint footprint.
        const float arrival=entity.order==Order::Patrol?20.0f:SustainedReturnTolerance;
        if(tacticsDistanceSquared(entity.pos,leashCenter)>arrival*arrival)return true;
        state.phase=SustainedOrderPhase::Travel;
        if(entity.order==Order::Patrol) {
            state.pursuitAnchor={};
            state.patrolTowardDestination=!state.patrolTowardDestination;
            entity.goal=state.patrolTowardDestination?state.patrolDestination:state.patrolOrigin;
        }
        resetNavigation(entity);
        return true;
    }
    if(entity.kind==Kind::Mender||definition(entity.kind).damage<=0)return true;
    if(entity.order==Order::Escort&&tacticsDistanceSquared(entity.pos,entity.goal)>
       SustainedReturnTolerance*SustainedReturnTolerance)return true;

    Entity* bestTarget=nullptr;float best=-std::numeric_limits<float>::max();
    const Vec2 center=entity.order==Order::Patrol?entity.pos:entity.goal;
    for(auto& enemy:entities_) {
        if(!eligible(&enemy,center))continue;
        const float dist=std::sqrt(tacticsDistanceSquared(entity.pos,enemy.pos));
        if(dist>definition(entity.kind).vision)continue;
        float score=1000.0f-dist;
        if(definition(enemy.kind).damage>0)score+=200.0f;
        if(entity.kind==Kind::Lancer&&(definition(enemy.kind).armor>=4||definition(enemy.kind).air))score+=180.0f;
        if(entity.kind==Kind::Mortar&&definition(enemy.kind).building)score+=180.0f;
        if(score>best||(score==best&&bestTarget&&enemy.id<bestTarget->id)) {best=score;bestTarget=&enemy;}
    }
    if(bestTarget) {
        state.pursuitTarget=bestTarget->id;state.phase=SustainedOrderPhase::Pursuit;
        if(entity.order==Order::Patrol)state.pursuitAnchor=entity.pos;
        resetNavigation(entity);
    }
    return true;
}

Entity* Simulation::sustainedCombatTarget(Entity& entity) {
    if(!refreshSustainedOrder(entity)||entity.sustained.phase!=SustainedOrderPhase::Pursuit)return nullptr;
    return get(entity.sustained.pursuitTarget);
}

void Simulation::clearSustainedReferences(Id destroyed) {
    for(auto& entity:entities_) {
        if(!entity.alive())continue;
        auto& state=entity.sustained;
        if(state.pursuitTarget==destroyed) {
            state.pursuitTarget=0;state.phase=SustainedOrderPhase::Return;resetNavigation(entity);
        }
        if(entity.order==Order::Escort&&state.escortTarget==destroyed) {
            const Vec2 lastFollow=entity.goal;
            resetCurrentOrder(entity,true);
            if(!activateNextOrder(entity)) {entity.order=Order::Defend;entity.goal=lastFollow;}
        }
    }
}
} // namespace cinder
