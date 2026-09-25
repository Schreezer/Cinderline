#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderCampaign.h"
#include "Tests/CinderCampaignTestDriver.h"

#include <algorithm>
#include <set>

namespace
{
bool SameAuthoredIds(const FCinderCampaignAuthoredIds& A, const FCinderCampaignAuthoredIds& B)
{
    return A.PlayerAnchor == B.PlayerAnchor && A.EnemyAnchor == B.EnemyAnchor
        && A.MiningWorker == B.MiningWorker && A.RallyWorker == B.RallyWorker
        && A.Kiln == B.Kiln && A.PlayerSiphon == B.PlayerSiphon
        && A.RemoteSiphon == B.RemoteSiphon && A.Resonator == B.Resonator
        && A.MotorPool == B.MotorPool && A.RaidProducer == B.RaidProducer
        && A.HomeOre == B.HomeOre && A.RemoteOre == B.RemoteOre
        && A.PracticePatrol == B.PracticePatrol && A.ScoutPosts == B.ScoutPosts
        && A.ForwardThreats == B.ForwardThreats && A.SiegeGuard == B.SiegeGuard
        && A.SiegeProducers == B.SiegeProducers;
}

bool ReachFirstShiftWorkerProduction(cinder::Simulation& Sim, FCinderCampaign& Campaign, FString& Failure)
{
    Campaign.CameraInput();
    Campaign.Observe(Sim, {});
    const cinder::Id Worker = Campaign.Authored().MiningWorker;
    Campaign.Observe(Sim, {Worker});
    if (Campaign.Phase() != 2)
        return CinderCampaignTestDriver::Fail(Failure, TEXT("Camera and Drudge selection did not reach Gather."));
    const cinder::Entity* WorkerEntity = Sim.find(Worker);
    const cinder::Id Ore = !Campaign.Authored().HomeOre.empty() ? Campaign.Authored().HomeOre.front()
        : (WorkerEntity ? WorkerEntity->resourceTarget : 0);
    cinder::Command Gather;
    Gather.type = cinder::CommandType::Gather;
    Gather.team = 0;
    Gather.units = {Worker};
    Gather.target = Ore;
    return CinderCampaignTestDriver::Issue(Sim, Campaign, Gather, Failure)
        && CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 3, 90, Failure);
}

bool ReachFiresFedDelivery(cinder::Simulation& Sim, FCinderCampaign& Campaign, FString& Failure)
{
    const FCinderCampaignAuthoredIds& Initial = Campaign.Authored();
    const cinder::Id HomeOre = Initial.HomeOre.empty() ? 0 : Initial.HomeOre.front();
    const cinder::Entity* HomeNode = Sim.find(HomeOre);
    cinder::Command Rally;
    Rally.type = cinder::CommandType::AutoRally;
    Rally.team = 0;
    Rally.kind = cinder::Kind::Headquarters;
    Rally.target = Initial.PlayerAnchor;
    Rally.point = HomeNode ? HomeNode->pos : Campaign.TargetPoint(ECinderCampaignTargetRole::HomeOre);
    if (!CinderCampaignTestDriver::Issue(Sim, Campaign, Rally, Failure)
        || !CinderCampaignTestDriver::Queue(Sim, Campaign, cinder::Kind::Worker, 1, Failure)
        || !CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 2, 120, Failure)
        || !CinderCampaignTestDriver::BuildNear(Sim, Campaign, cinder::Kind::Processor,
            Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
        || !CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 3, 180, Failure)) return false;

    cinder::Command Gather;
    Gather.type = cinder::CommandType::Gather;
    Gather.team = 0;
    Gather.units = {CinderCampaignTestDriver::FirstFriendly(Sim, cinder::Kind::Worker)};
    Gather.target = Campaign.Authored().RemoteOre.empty() ? 0 : Campaign.Authored().RemoteOre.front();
    if (!CinderCampaignTestDriver::Issue(Sim, Campaign, Gather, Failure)
        || !CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 4, 10, Failure)
        || !CinderCampaignTestDriver::BuildNear(Sim, Campaign, cinder::Kind::Processor,
            Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
        || !CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 5, 180, Failure)) return false;
    return Campaign.IsRunning() && Campaign.Phase() == 5;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignScenarioDeterminism,
    "Cinderline.Campaign.ScenarioDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignScenarioDeterminism::RunTest(const FString& Parameters)
{
    (void)Parameters;
    for (int32 Mission = 0; Mission < FCinderCampaign::MissionCount; ++Mission)
    {
        const FCinderCampaignMissionDefinition& Definition = FCinderCampaign::Definition(Mission);
        TestTrue(*FString::Printf(TEXT("Mission %d has a name"), Mission + 1), !Definition.Name.IsEmpty());
        TestTrue(*FString::Printf(TEXT("Mission %d has its agreed objective count"), Mission + 1),
            Definition.PhaseCount == (Mission == 0 ? 8 : Mission == 1 ? 6 : Mission == 2 ? 5 : Mission == 3 ? 5 : Mission == 4 ? 6 : 1));

        cinder::Config Expected;
        TestTrue(*FString::Printf(TEXT("Mission %d exposes an expected configuration"), Mission + 1),
            FCinderCampaign::ExpectedConfig(Mission, Expected));

        cinder::Simulation FirstSim;
        cinder::Simulation SecondSim;
        FCinderCampaign First;
        FCinderCampaign Second;
        if (!TestTrue(*FString::Printf(TEXT("Mission %d initializes"), Mission + 1),
            First.InitializeMission(FirstSim, Mission) && Second.InitializeMission(SecondSim, Mission))) return false;
        TestEqual(*FString::Printf(TEXT("Mission %d produces the same simulation hash"), Mission + 1),
            static_cast<uint64>(FirstSim.stateHash()), static_cast<uint64>(SecondSim.stateHash()));
        TestTrue(*FString::Printf(TEXT("Mission %d resolves the same authored actors"), Mission + 1),
            SameAuthoredIds(First.Authored(), Second.Authored()));
        TestTrue(*FString::Printf(TEXT("Mission %d uses its declared map, length, AI and player count"), Mission + 1),
            FirstSim.config().map == Expected.map && FirstSim.config().seed == Expected.seed
            && FirstSim.config().matchLength == Expected.matchLength && FirstSim.config().ai == Expected.ai
            && FirstSim.config().playerCount == Expected.playerCount);
    }

    cinder::Config Invalid;
    TestFalse(TEXT("An out-of-range mission has no expected configuration"),
        FCinderCampaign::ExpectedConfig(FCinderCampaign::MissionCount, Invalid));
    const FCinderCampaignMissionDefinition& Finale = FCinderCampaign::Definition(5);
    cinder::Config FinaleConfig;
    TestTrue(TEXT("Trial by Fire config is available"), FCinderCampaign::ExpectedConfig(5, FinaleConfig));
    TestTrue(TEXT("Trial by Fire is one ordinary Standard match with Normal AI"),
        Finale.PhaseCount == 1 && Finale.bNormalAI && FinaleConfig.ai
        && FinaleConfig.aiAggression == 1.0f && FinaleConfig.matchLength == cinder::MatchLength::Standard
        && FinaleConfig.playerCount == 2);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignTransactionalLifecycle,
    "Cinderline.Campaign.TransactionalLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignTransactionalLifecycle::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderCampaign Campaign;
    if (!TestTrue(TEXT("A valid authored mission initializes"), Campaign.InitializeMission(Sim, 2))) return false;
    const uint64 HashBefore = Sim.stateHash();
    const FCinderCampaignState StateBefore = Campaign.ExportState();
    TestFalse(TEXT("Invalid mission initialization is rejected"), Campaign.InitializeMission(Sim, -1));
    TestEqual(TEXT("Rejected initialization preserves the active simulation"),
        static_cast<uint64>(Sim.stateHash()), HashBefore);
    TestTrue(TEXT("Rejected initialization preserves director state"),
        Campaign.MissionIndex() == StateBefore.Mission && Campaign.Phase() == StateBefore.Phase
        && Campaign.Outcome() == StateBefore.Outcome && SameAuthoredIds(Campaign.Authored(), StateBefore.Authored));

    FCinderCampaignState Corrupt = StateBefore;
    Corrupt.Mission = FCinderCampaign::MissionCount;
    FString Error;
    TestFalse(TEXT("A state for an invalid mission fails validation"),
        FCinderCampaign::ValidateState(Sim, Corrupt, Error));
    TestTrue(TEXT("Invalid state reports a useful validation error"), !Error.IsEmpty());
    Error.Reset();
    TestFalse(TEXT("Import rejects invalid state"), Campaign.ImportState(Sim, Corrupt, Error));
    TestTrue(TEXT("Rejected import leaves the live director untouched"),
        Campaign.MissionIndex() == StateBefore.Mission && Campaign.Phase() == StateBefore.Phase
        && Campaign.Outcome() == StateBefore.Outcome);
    Corrupt = StateBefore;
    Corrupt.Authored.PlayerAnchor = Corrupt.Authored.EnemyAnchor;
    Error.Reset();
    TestFalse(TEXT("Semantic validation rejects an enemy actor stored as the player Anchor"),
        FCinderCampaign::ValidateState(Sim, Corrupt, Error));
    TestTrue(TEXT("Semantic role rejection reports a useful error"), !Error.IsEmpty());

    cinder::Simulation DefeatSim;
    FCinderCampaign Defeat;
    TestTrue(TEXT("A mission for loss precedence initializes"), Defeat.InitializeMission(DefeatSim, 0));
    DefeatSim.forfeit(0);
    Defeat.Observe(DefeatSim, {});
    TestTrue(TEXT("Player elimination immediately produces campaign defeat"),
        Defeat.IsTerminal() && Defeat.Outcome() == ECinderCampaignOutcome::Defeat);

    cinder::Simulation FinaleSim;
    FCinderCampaign FinaleCampaign;
    TestTrue(TEXT("Trial by Fire initializes"), FinaleCampaign.InitializeMission(FinaleSim, 5));
    FinaleSim.forfeit(1);
    FinaleCampaign.Observe(FinaleSim, {});
    TestTrue(TEXT("An early simulation victory immediately completes Trial by Fire"),
        FinaleSim.winner() == 0 && FinaleCampaign.Outcome() == ECinderCampaignOutcome::Victory);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignCommandAndReplacementGates,
    "Cinderline.Campaign.CommandAndReplacementGates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignCommandAndReplacementGates::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderCampaign Campaign;
    FString Failure;
    if (!TestTrue(TEXT("First Shift initializes"), Campaign.InitializeMission(Sim, 0))) return false;
    const bool bReachedProduction = ReachFirstShiftWorkerProduction(Sim, Campaign, Failure);
    if (!TestTrue(*FString::Printf(TEXT("Ordinary camera, selection, Gather and delivery reach production: %s"), *Failure),
        bReachedProduction)) return false;

    cinder::Command Rejected;
    Rejected.type = cinder::CommandType::Train;
    Rejected.team = 0;
    Rejected.kind = cinder::Kind::Worker;
    TestFalse(TEXT("A producer-less Train command is rejected by normal rules"), Sim.command(Rejected).accepted);
    const cinder::Vec2 Base = Campaign.TargetPoint(ECinderCampaignTargetRole::PlayerAnchor);
    Sim.debugSpawn(cinder::Kind::Worker, 0, {Base.x, Base.y + 220.0f});
    Campaign.Observe(Sim, {});
    TestEqual(TEXT("A rejected command and an already present extra worker cannot advance the production gate"),
        Campaign.Phase(), 3);

    TestTrue(TEXT("A replacement Drudge can be queued through paid production"),
        CinderCampaignTestDriver::Queue(Sim, Campaign, cinder::Kind::Worker, 1, Failure));
    const cinder::Id Anchor = Campaign.Authored().PlayerAnchor;
    cinder::Command Cancel;
    Cancel.type = cinder::CommandType::CancelQueue;
    Cancel.team = 0;
    Cancel.units = {Anchor};
    Cancel.queueIndex = 0;
    TestTrue(TEXT("The accepted Drudge job can be canceled normally"),
        CinderCampaignTestDriver::Issue(Sim, Campaign, Cancel, Failure));
    const int32 ProducedBefore = Sim.players()[0].stats.produced;
    for (int32 Second = 0; Second < 45; ++Second)
        CinderCampaignTestDriver::AdvanceOneSecond(Sim, Campaign);
    TestTrue(TEXT("Canceling the queue leaves the completed-result objective active"),
        Campaign.Phase() == 3 && Sim.players()[0].stats.produced == ProducedBefore);

    Failure.Reset();
    const bool bReplacementRecovered = CinderCampaignTestDriver::Queue(Sim, Campaign, cinder::Kind::Worker, 1, Failure)
        && CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 4, 120, Failure);
    TestTrue(*FString::Printf(TEXT("A newly paid replacement recovers the objective: %s"), *Failure),
        bReplacementRecovered);
    TestTrue(TEXT("Only the completed replacement advances First Shift"),
        Campaign.Phase() == 4 && Sim.players()[0].stats.produced > ProducedBefore);

    if (bReplacementRecovered)
    {
        const cinder::Id Builder = CinderCampaignTestDriver::FirstFriendly(Sim, cinder::Kind::Worker);
        const cinder::Entity* Worker = Sim.find(Builder);
        cinder::Command Move;
        Move.type = cinder::CommandType::Move; Move.team = 0; Move.units = {Builder};
        Move.point = Worker ? Worker->pos : Base;
        TestTrue(TEXT("A builder can finish a movement before the guided build"),
            CinderCampaignTestDriver::Issue(Sim, Campaign, Move, Failure));
        cinder::Command Plan;
        Plan.type = cinder::CommandType::Build; Plan.team = 0; Plan.units = {Builder};
        Plan.kind = cinder::Kind::Foundry;
        Plan.point = Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite);
        Plan.queueMode = cinder::CommandQueueMode::Append;
        if (TestTrue(TEXT("The guided Kiln can be queued as an unpaid plan"),
            CinderCampaignTestDriver::Issue(Sim, Campaign, Plan, Failure)))
        {
            TestEqual(TEXT("A queued site has no authored foundation yet"), Campaign.Authored().Kiln, cinder::Id(0));
            TestTrue(*FString::Printf(TEXT("Observation recognizes the later paid foundation and completion: %s"), *Failure),
                CinderCampaignTestDriver::WaitForPhase(Sim, Campaign, 5, 180, Failure));
        }
    }

    cinder::Simulation FinalLoadSim;
    FCinderCampaign FinalLoadCampaign;
    FString FinalLoadFailure;
    if (!TestTrue(TEXT("Final-load recovery fixture initializes"),
        FinalLoadCampaign.InitializeMission(FinalLoadSim, 1))) return false;
    if (!TestTrue(TEXT("Final-load fixture reaches remote delivery through ordinary commands"),
        ReachFiresFedDelivery(FinalLoadSim, FinalLoadCampaign, FinalLoadFailure))) return false;
    const std::vector<cinder::Id> FinalLoadOre = FinalLoadCampaign.Authored().RemoteOre;
    const cinder::Id FinalLoadWorker = FinalLoadCampaign.Authored().MiningWorker;
    const cinder::Entity* InitialWorker = FinalLoadSim.find(FinalLoadWorker);
    float PreviousCarried = InitialWorker ? InitialWorker->carried : 0.0f;
    bool bCollectedRemoteOre = false;
    for (int32 Tick = 0; Tick < 2400; ++Tick)
    {
        const cinder::Entity* Worker = FinalLoadSim.find(FinalLoadWorker);
        if (Worker && Worker->alive())
        {
            // The relocated builder can still carry ore gathered at home. Wait
            // for a real harvest from this patch and its ensuing return trip.
            if (Worker->carried > PreviousCarried + 0.001f &&
                std::find(FinalLoadOre.begin(), FinalLoadOre.end(), Worker->resourceTarget) != FinalLoadOre.end())
                bCollectedRemoteOre = true;
            if (bCollectedRemoteOre && Worker->returning && Worker->carried > 0.0f) break;
            PreviousCarried = Worker->carried;
        }
        FinalLoadSim.update(cinder::Simulation::Step);
    }
    const cinder::Entity* CarryingWorker = FinalLoadSim.find(FinalLoadWorker);
    if (!TestTrue(TEXT("The assigned remote Drudge harvests and starts returning with a final load"),
        bCollectedRemoteOre && CarryingWorker && CarryingWorker->alive() && CarryingWorker->returning &&
        CarryingWorker->carried > 0.0f)) return false;
    for (cinder::Id OreId : FinalLoadOre)
        if (cinder::Entity* Ore = const_cast<cinder::Entity*>(FinalLoadSim.find(OreId))) Ore->resource = 0.0f;
    FinalLoadCampaign.Observe(FinalLoadSim, {});
    TestTrue(TEXT("Depletion preserves the authored patch while its Drudge carries the last load"),
        FinalLoadCampaign.Authored().RemoteOre == FinalLoadOre && FinalLoadCampaign.IsRunning());
    const bool bFinalLoadDelivered = CinderCampaignTestDriver::WaitUntil(FinalLoadSim, FinalLoadCampaign,
        [&] { return FinalLoadCampaign.IsTerminal(); }, 120,
        TEXT("The final carried load did not complete at the authored Siphon."), FinalLoadFailure);
    TestTrue(*FString::Printf(TEXT("The last-node final load completes without an extra retarget: %s"),
        *FinalLoadFailure), bFinalLoadDelivered
        && FinalLoadCampaign.Outcome() == ECinderCampaignOutcome::Victory
        && FinalLoadCampaign.Authored().RemoteOre == FinalLoadOre);

    cinder::Simulation RecoverySim;
    FCinderCampaign RecoveryCampaign;
    FString RecoveryFailure;
    if (!TestTrue(TEXT("Remote-patch recovery fixture initializes"),
        RecoveryCampaign.InitializeMission(RecoverySim, 1))) return false;
    const bool bReachedDelivery = ReachFiresFedDelivery(RecoverySim, RecoveryCampaign, RecoveryFailure);
    if (!TestTrue(*FString::Printf(TEXT("Ordinary commands reach the remote delivery boundary: %s"),
        *RecoveryFailure), bReachedDelivery)) return false;
    const FCinderCampaignState BeforeRetarget = RecoveryCampaign.ExportState();
    FCinderCampaignState Arranged = BeforeRetarget;
    Arranged.bRemoteWorkerCarried = true;
    Arranged.bRemoteDeliveryObserved = false;
    Arranged.Counters.LastTrackedCarried = 18.0f;
    FString ImportError;
    if (!TestTrue(TEXT("Delivery-history arrangement remains a semantically valid phase-five state"),
        RecoveryCampaign.ImportState(RecoverySim, Arranged, ImportError))) return false;
    for (cinder::Id OreId : BeforeRetarget.Authored.RemoteOre)
        if (cinder::Entity* Ore = const_cast<cinder::Entity*>(RecoverySim.find(OreId))) Ore->resource = 0.0f;
    for (const cinder::Entity& Entity : RecoverySim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Worker)
            const_cast<cinder::Entity&>(Entity).hp = 0.0f;
    RecoveryCampaign.Observe(RecoverySim, {});
    TestTrue(TEXT("A depleted patch is retained when no complete Drudge can prove a recovery route"),
        RecoveryCampaign.Authored().RemoteOre == BeforeRetarget.Authored.RemoteOre
        && RecoveryCampaign.IsRunning());
    const int32 RecoveryBudget = cinder::definition(cinder::Kind::Worker).cost
        + cinder::definition(cinder::Kind::Processor).cost;
    RecoverySim.debugResources(0, FMath::Max(RecoverySim.players()[0].ore, RecoveryBudget));
    const int32 ReplacementOreBefore = RecoverySim.players()[0].ore;
    if (!TestTrue(TEXT("The Anchor accepts a normally paid replacement Drudge"),
        CinderCampaignTestDriver::Queue(RecoverySim, RecoveryCampaign,
            cinder::Kind::Worker, 1, RecoveryFailure))) return false;
    TestEqual(TEXT("The replacement Drudge is charged at the normal production cost"),
        RecoverySim.players()[0].ore,
        ReplacementOreBefore - cinder::definition(cinder::Kind::Worker).cost);
    if (!TestTrue(TEXT("The cached recovery search retries after the replacement Drudge completes"),
        CinderCampaignTestDriver::WaitUntil(RecoverySim, RecoveryCampaign,
            [&] { return RecoveryCampaign.Authored().RemoteOre != BeforeRetarget.Authored.RemoteOre; },
            120, TEXT("A replacement Drudge did not unlock reachable patch recovery."), RecoveryFailure))) return false;
    const FCinderCampaignState AfterRetarget = RecoveryCampaign.ExportState();
    TestTrue(TEXT("Depletion replaces the authored ore IDs with a different live patch"),
        AfterRetarget.Authored.RemoteOre != BeforeRetarget.Authored.RemoteOre
        && !AfterRetarget.Authored.RemoteOre.empty()
        && std::all_of(AfterRetarget.Authored.RemoteOre.begin(), AfterRetarget.Authored.RemoteOre.end(),
            [&RecoverySim](cinder::Id Id)
            {
                const cinder::Entity* Ore = RecoverySim.find(Id);
                return Ore && Ore->alive() && Ore->kind == cinder::Kind::Resource && Ore->resource > 0.0f;
            }));
    TestTrue(TEXT("Retargeting moves both the patch marker and proposed Siphon site"),
        (AfterRetarget.RemotePatchPoint.x != BeforeRetarget.RemotePatchPoint.x
            || AfterRetarget.RemotePatchPoint.y != BeforeRetarget.RemotePatchPoint.y)
        && (AfterRetarget.BuildPoint.x != BeforeRetarget.BuildPoint.x
            || AfterRetarget.BuildPoint.y != BeforeRetarget.BuildPoint.y));
    TestTrue(TEXT("Retargeting clears stale carry state and keeps delivery unearned"),
        !AfterRetarget.bRemoteWorkerCarried && !AfterRetarget.bRemoteDeliveryObserved
        && AfterRetarget.Counters.LastTrackedCarried == 0.0f);
    TestTrue(TEXT("Retargeting alone does not award mission victory"),
        RecoveryCampaign.IsRunning() && RecoveryCampaign.Phase() == 5);

    const cinder::Vec2 NewBuildSite = RecoveryCampaign.TargetPoint(ECinderCampaignTargetRole::BuildSite);
    if (!TestTrue(TEXT("Ordinary Move sends a surviving Drudge to reveal the retargeted build area"),
        CinderCampaignTestDriver::Move(RecoverySim, RecoveryCampaign,
            CinderCampaignTestDriver::CompleteFriendly(RecoverySim, cinder::Kind::Worker), NewBuildSite,
            cinder::CommandType::Move, RecoveryFailure)
        && CinderCampaignTestDriver::WaitUntil(RecoverySim, RecoveryCampaign,
            [&] { return RecoverySim.visible(0, NewBuildSite); }, 180,
            TEXT("No surviving Drudge reached the retargeted build area."), RecoveryFailure))) return false;
    if (!TestTrue(TEXT("A new Siphon can be built at the reachable retargeted patch"),
        CinderCampaignTestDriver::BuildNear(RecoverySim, RecoveryCampaign, cinder::Kind::Processor,
            NewBuildSite, RecoveryFailure))) return false;
    const cinder::Vec2 NewPatch = RecoveryCampaign.TargetPoint(ECinderCampaignTargetRole::RemoteOre);
    if (!TestTrue(TEXT("The retargeted Siphon completes before delivery"),
        CinderCampaignTestDriver::WaitUntil(RecoverySim, RecoveryCampaign, [&]
            {
                for (const cinder::Entity& Entity : RecoverySim.entities())
                {
                    const float X = Entity.pos.x - NewPatch.x;
                    const float Y = Entity.pos.y - NewPatch.y;
                    if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Processor
                        && Entity.progress >= 1.0f && X * X + Y * Y <= 520.0f * 520.0f) return true;
                }
                return false;
            }, 180, TEXT("The retargeted Siphon did not complete."), RecoveryFailure))) return false;
    cinder::Command NewGather;
    NewGather.type = cinder::CommandType::Gather;
    NewGather.team = 0;
    NewGather.units = {CinderCampaignTestDriver::FirstFriendly(RecoverySim, cinder::Kind::Worker)};
    NewGather.target = RecoveryCampaign.Authored().RemoteOre.front();
    if (!TestTrue(TEXT("A normal Gather command accepts an ore ID from the replacement patch"),
        CinderCampaignTestDriver::Issue(RecoverySim, RecoveryCampaign, NewGather, RecoveryFailure))) return false;
    const bool bFreshDelivery = CinderCampaignTestDriver::WaitUntil(RecoverySim, RecoveryCampaign,
        [&] { return RecoveryCampaign.IsTerminal(); }, 240,
        TEXT("Fresh ore did not reach the replacement Siphon."), RecoveryFailure);
    TestTrue(*FString::Printf(TEXT("Only a fresh carry and drop at the new patch completes the mission: %s"),
        *RecoveryFailure), bFreshDelivery && RecoveryCampaign.Outcome() == ECinderCampaignOutcome::Victory);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignAuthoredMissionsEndToEnd,
    "Cinderline.Campaign.AuthoredMissionsEndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignAuthoredMissionsEndToEnd::RunTest(const FString& Parameters)
{
    (void)Parameters;
    for (int32 Mission = 0; Mission < 5; ++Mission)
    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(*FString::Printf(TEXT("Authored mission %d initializes"), Mission + 1),
            Campaign.InitializeMission(Sim, Mission))) return false;
        const int32 PlayerProducedBefore = Sim.players()[0].stats.produced;
        const int32 PlayerBuiltBefore = Sim.players()[0].stats.built;
        FString Failure;
        const bool bCompleted = CinderCampaignTestDriver::DriveAuthoredMission(Sim, Campaign, Failure);
        if (!TestTrue(*FString::Printf(TEXT("Mission %d completes through ordinary accepted commands: %s"),
            Mission + 1, *Failure), bCompleted)) return false;
        TestTrue(*FString::Printf(TEXT("Mission %d reports campaign victory"), Mission + 1),
            Campaign.IsTerminal() && Campaign.Outcome() == ECinderCampaignOutcome::Victory);
        const FCinderCampaignState FinalState = Campaign.ExportState();
        const uint64 EarlierObjectives = (uint64{1} << (Campaign.PhaseCount() - 1)) - 1;
        TestTrue(*FString::Printf(TEXT("Mission %d traversed every authored phase before victory"), Mission + 1),
            Campaign.Phase() == Campaign.PhaseCount() - 1
            && (FinalState.CompletedObjectiveMask & EarlierObjectives) == EarlierObjectives);
        if (Mission == 0 || Mission == 1 || Mission == 2 || Mission == 4)
            TestTrue(*FString::Printf(TEXT("Mission %d completed real paid player production"), Mission + 1),
                Sim.players()[0].stats.produced > PlayerProducedBefore);
        if (Mission == 0 || Mission == 1 || Mission == 3 || Mission == 4)
            TestTrue(*FString::Printf(TEXT("Mission %d completed real player construction"), Mission + 1),
                Sim.players()[0].stats.built > PlayerBuiltBefore);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignPaidWaveBudget,
    "Cinderline.Campaign.PaidWaveBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignPaidWaveBudget::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderCampaign Campaign;
    if (!TestTrue(TEXT("Hold the Line initializes"), Campaign.InitializeMission(Sim, 3))) return false;
    const int32 ProducedBefore = Sim.players()[1].stats.produced;
    FString Failure;
    bool bDispatchedWhileQueuePending = false;
    const bool bCompleted = CinderCampaignTestDriver::DriveAuthoredMission(
        Sim, Campaign, Failure, &bDispatchedWhileQueuePending);
    if (!TestTrue(*FString::Printf(TEXT("Hold the Line resolves its finite waves: %s"), *Failure),
        bCompleted)) return false;

    const FCinderCampaignState State = Campaign.ExportState();
    int32 PaidQuantities = 0;
    int32 TrainCommands = 0;
    for (const cinder::RecordedCommand& Recorded : Sim.recording())
    {
        const cinder::Command& Command = Recorded.command;
        if (Command.team != 1 || (Command.type != cinder::CommandType::Train
            && Command.type != cinder::CommandType::AutoTrain)) continue;
        ++TrainCommands;
        PaidQuantities += Command.type == cinder::CommandType::AutoTrain ? Command.queueIndex : 1;
    }
    const int32 ProducedByWaves = Sim.players()[1].stats.produced - ProducedBefore;
    const std::set<cinder::Id> UniqueProduced(State.Authored.OrderedUnits.begin(), State.Authored.OrderedUnits.end());
    const bool bFinalWaveDead = !State.Authored.ActiveWave.empty()
        && std::all_of(State.Authored.ActiveWave.begin(), State.Authored.ActiveWave.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return !Entity || !Entity->alive();
        });
    TestTrue(TEXT("The opponent issued the authored three-unit and four-unit paid wave budgets"),
        TrainCommands == 7 && PaidQuantities == 7 && State.Counters.OpponentTrainOrders == 4);
    TestTrue(TEXT("A completed raider dispatches while later paid wave output remains queued"),
        bDispatchedWhileQueuePending);
    TestTrue(TEXT("Every completed wave unit has one stable tracked ID"),
        ProducedByWaves > 0 && static_cast<int32>(UniqueProduced.size()) == ProducedByWaves
        && static_cast<int32>(State.Authored.OrderedUnits.size()) == ProducedByWaves);
    TestTrue(TEXT("No free wave unit appeared beyond quantities paid for by recorded commands"),
        ProducedByWaves <= PaidQuantities);
    TestTrue(TEXT("Both bounded waves finished and were cleared before victory"),
        State.Counters.WaveIndex == 1 && State.bWaveProductionFinished
        && bFinalWaveDead && Campaign.Outcome() == ECinderCampaignOutcome::Victory);

    cinder::Simulation ClosedSim;
    FCinderCampaign ClosedCampaign;
    FString ClosedFailure;
    if (!TestTrue(TEXT("Producer-loss recovery fixture initializes"), ClosedCampaign.InitializeMission(ClosedSim, 3))) return false;
    cinder::Command Rally;
    Rally.type = cinder::CommandType::AutoRally;
    Rally.team = 0;
    Rally.kind = cinder::Kind::Resource;
    Rally.point = ClosedCampaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense);
    if (!TestTrue(TEXT("Recovery fixture accepts the shared army rally"),
        CinderCampaignTestDriver::Issue(ClosedSim, ClosedCampaign, Rally, ClosedFailure))) return false;
    if (!TestTrue(TEXT("Recovery fixture builds its Ward normally"),
        CinderCampaignTestDriver::BuildNear(ClosedSim, ClosedCampaign, cinder::Kind::Turret,
            ClosedCampaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense), ClosedFailure)
        && CinderCampaignTestDriver::WaitForPhase(ClosedSim, ClosedCampaign, 2, 180, ClosedFailure))) return false;
    const cinder::Id ProducerId = ClosedCampaign.Authored().RaidProducer;
    const cinder::Entity* Producer = ClosedSim.find(ProducerId);
    if (!TestTrue(TEXT("Recovery fixture has its paid-wave producer"), Producer && Producer->alive())) return false;
    if (!TestTrue(TEXT("Ordinary combat can close the producer before either wave begins"),
        CinderCampaignTestDriver::Move(ClosedSim, ClosedCampaign,
            CinderCampaignTestDriver::CompleteAttackers(ClosedSim), Producer->pos,
            cinder::CommandType::AttackMove, ClosedFailure)
        && CinderCampaignTestDriver::WaitUntil(ClosedSim, ClosedCampaign, [&]
            {
                const cinder::Entity* Current = ClosedSim.find(ProducerId);
                return !Current || !Current->alive();
            }, 180, TEXT("The ordinary force did not destroy the wave producer."), ClosedFailure))) return false;
    cinder::Command Defend;
    Defend.type = cinder::CommandType::Defend;
    Defend.team = 0;
    Defend.units = CinderCampaignTestDriver::CompleteAttackers(ClosedSim);
    Defend.point = ClosedCampaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense);
    if (!TestTrue(TEXT("Surviving troops accept Defend after closing production"),
        CinderCampaignTestDriver::Issue(ClosedSim, ClosedCampaign, Defend, ClosedFailure))) return false;
    const uint64 ClosedPhaseTick = ClosedSim.tick();
    const bool bClosedResolved = CinderCampaignTestDriver::WaitUntil(ClosedSim, ClosedCampaign,
        [&] { return ClosedCampaign.IsTerminal(); }, 5,
        TEXT("Closed production left an empty wave waiting forever."), ClosedFailure);
    TestTrue(*FString::Printf(TEXT("A destroyed producer resolves both empty finite waves: %s; %s"),
        *ClosedFailure, *CinderCampaignTestDriver::Diagnostics(ClosedSim, ClosedCampaign)), bClosedResolved);
    TestTrue(*FString::Printf(TEXT("Destroyed-producer recovery ends in victory (winner=%d outcome=%d)"),
        ClosedSim.winner(), static_cast<int32>(ClosedCampaign.Outcome())),
        ClosedCampaign.Outcome() == ECinderCampaignOutcome::Victory);
    TestTrue(TEXT("Closed production bypasses both fake preparation waits in two campaign cycles"),
        ClosedSim.tick() - ClosedPhaseTick <= 40);
    TestEqual(TEXT("Destroyed-producer recovery creates no hidden wave units"),
        ClosedSim.players()[1].stats.produced, 0);
    return !HasAnyErrors();
}

#endif
