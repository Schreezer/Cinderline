#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "Sim/Simulation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

using namespace cinder;

namespace {
constexpr double Pi=3.14159265358979323846;
struct Color { CGFloat r,g,b,a; };
const Color Navy{.026,.043,.074,1}, Panel{.041,.065,.099,.98}, Edge{.13,.22,.28,1},
 Ink{.83,.91,.93,1}, Muted{.40,.54,.61,1}, Cyan{.22,.91,.84,1}, Coral{1,.37,.29,1}, Amber{1,.77,.40,1};
struct Camera {
 double x=1100,y=1100,zoom=.6;
 NSPoint screen(Vec2 p,NSRect rect) const { return NSMakePoint(NSMidX(rect)+(p.x-x)*zoom,NSMidY(rect)+(p.y-y)*zoom*.72); }
 Vec2 world(NSPoint p,NSRect rect) const { return {(float)(x+(p.x-NSMidX(rect))/zoom),(float)(y+(p.y-NSMidY(rect))/(zoom*.72))}; }
 static double minimumZoom(NSRect rect) {return std::max({.18,rect.size.width/Simulation::WorldSize,rect.size.height/(Simulation::WorldSize*.72)});}
 void clamp(NSRect rect) {
  zoom=std::max(zoom,minimumZoom(rect));
  const double halfWidth=std::min((double)Simulation::WorldSize/2,rect.size.width/(2*zoom));
  const double halfHeight=std::min((double)Simulation::WorldSize/2,rect.size.height/(2*zoom*.72));
  x=std::clamp(x,halfWidth,Simulation::WorldSize-halfWidth);y=std::clamp(y,halfHeight,Simulation::WorldSize-halfHeight);
 }
};
struct UIButton {
 NSRect rect; std::string label,detail; int action=0; Kind kind=Kind::Worker; int parameter=0; bool enabled=true;
};
enum Action { Begin=1,Map0,Map1,Map2,Resume,Rematch,Help,QuitMenu,Train=100,Build,AttackMode,Stop,Hold,Research,Cancel,Rally,Army,Home,Save,Load,FilterType,GroupPage,ResumeBuild };
void filterSelection(std::unordered_set<Id>& selected,const Simulation& sim,Kind kind) {
 for(auto i=selected.begin();i!=selected.end();){const Entity* e=sim.find(*i);if(!e||!e->alive()||e->kind!=kind)i=selected.erase(i);else ++i;}
}
std::string constructionStatus(const Simulation& sim,Id foundation) {
 if(sim.constructionActive(foundation))return "CONSTRUCTING";
 return sim.constructionWorker(foundation)?"DRUDGE EN ROUTE":"PAUSED - NEEDS DRUDGE";
}
NSRect subgroupRect(NSRect area,int index) {
 const double gap=3,width=(area.size.width-2*gap)/3,height=(area.size.height-2*gap)/3;
 return NSMakeRect(area.origin.x+(index%3)*(width+gap),area.origin.y+(index/3)*(height+gap),width,height);
}
void fill(CGContextRef c,Color col) { CGContextSetRGBFillColor(c,col.r,col.g,col.b,col.a); }
void stroke(CGContextRef c,Color col) { CGContextSetRGBStrokeColor(c,col.r,col.g,col.b,col.a); }
void box(CGContextRef c,NSRect r,Color col) { fill(c,col); CGContextFillRect(c,NSRectToCGRect(r)); }
void borderRect(CGContextRef c,NSRect r,Color col,double width=1) { stroke(c,col); CGContextSetLineWidth(c,width); CGContextStrokeRect(c,NSRectToCGRect(r)); }
void line(CGContextRef c,NSPoint a,NSPoint b,Color col,double width=1) { stroke(c,col); CGContextSetLineWidth(c,width); CGContextBeginPath(c); CGContextMoveToPoint(c,a.x,a.y); CGContextAddLineToPoint(c,b.x,b.y); CGContextStrokePath(c); }
void oval(CGContextRef c,NSRect r,Color col) { fill(c,col); CGContextFillEllipseInRect(c,NSRectToCGRect(r)); }
void ring(CGContextRef c,NSRect r,Color col,double width=1) { stroke(c,col); CGContextSetLineWidth(c,width); CGContextStrokeEllipseInRect(c,NSRectToCGRect(r)); }
void poly(CGContextRef c,std::initializer_list<NSPoint> points,Color col,Color border={0,0,0,0},double width=1) {
 if(points.size()==0)return; CGContextBeginPath(c); auto it=points.begin(); CGContextMoveToPoint(c,it->x,it->y);
 for(++it;it!=points.end();++it)CGContextAddLineToPoint(c,it->x,it->y); CGContextClosePath(c); fill(c,col);
 if(border.a>0){stroke(c,border);CGContextSetLineWidth(c,width);CGContextDrawPath(c,kCGPathFillStroke);}else CGContextFillPath(c);
}
void label(std::string s,NSRect r,double size,Color col=Ink,bool bold=false,bool mono=false,NSTextAlignment alignment=NSTextAlignmentLeft) {
 NSString* text=[[NSString alloc] initWithUTF8String:s.c_str()]; if(!text)return;
 NSMutableParagraphStyle* ps=[[NSMutableParagraphStyle alloc] init]; ps.alignment=alignment; ps.lineBreakMode=NSLineBreakByTruncatingTail;
 // AppKit font lookup can return nil. A dictionary literal then raises an
 // NSInvalidArgumentException, including while drawing the static map subtitle.
 // Resolve each font once, retain it, and insert only available attributes.
 static NSMutableDictionary<NSNumber*,NSFont*>* fonts=[NSMutableDictionary dictionary];
 NSNumber* fontKey=@((int)std::lround(size*10)*4+(bold?2:0)+(mono?1:0));
 NSFont* font=fonts[fontKey];
 if(!font){
  font=mono?[NSFont monospacedSystemFontOfSize:size weight:bold?NSFontWeightSemibold:NSFontWeightRegular]:[NSFont systemFontOfSize:size weight:bold?NSFontWeightSemibold:NSFontWeightRegular];
  if(!font){fprintf(stderr,"Cinderline: font lookup unavailable (size %.1f, mono %d); using fallback.\n",size,mono);font=[NSFont fontWithName:mono?@"Menlo":@"Helvetica" size:size];}
  if(!font)font=[NSFont systemFontOfSize:size];
  if(font)fonts[fontKey]=font;
 }
 NSColor* color=[NSColor colorWithSRGBRed:col.r green:col.g blue:col.b alpha:col.a];
 NSMutableDictionary* attributes=[NSMutableDictionary dictionary];
 if(font)attributes[NSFontAttributeName]=font;
 if(color)attributes[NSForegroundColorAttributeName]=color;
 if(ps)attributes[NSParagraphStyleAttributeName]=ps;
 CGContextRef context=[[NSGraphicsContext currentContext] CGContext];CGContextSaveGState(context);CGContextClipToRect(context,NSRectToCGRect(r));
 [text drawInRect:r withAttributes:attributes];CGContextRestoreGState(context);
}
std::string clockText(float t) { char buf[30];snprintf(buf,sizeof(buf),"%02d:%02d",int(t)/60,int(t)%60);return buf; }
float hash(float x,float y) { double a=sin(x*12.9898+y*78.233)*43758.5453;return (float)(a-floor(a)); }
NSRect dragRect(NSPoint a,NSPoint b) { return NSMakeRect(std::min(a.x,b.x),std::min(a.y,b.y),fabs(a.x-b.x),fabs(a.y-b.y)); }
}

@interface CinderlineView : NSView {
 Simulation sim;
 Camera camera;
 std::unordered_set<Id> selected;
 std::vector<UIButton> buttons;
 std::array<bool,128> keys;
 NSPoint mouse,down,lastDrag;
 BOOL menu,paused,showHelp,dragging,panning,minimapDrag,spaceHeld,buildingMode,attackMode,rallyMode,aTapEligible;
 Kind placement;
 int mapChoice,subgroupPage;
 double lastFrame,toastUntil,commandPulse,elapsedAccumulator,aKeyStart;
 float fps;
 std::string toast,lastSimAlert;
 std::array<std::vector<Id>,10> controlGroups;
 std::unordered_map<Id,float> observedBuildingHP;
 double criticalUntil,attackAlertCooldown;
 Vec2 pulsePoint;
 NSTimer* timer;
 NSString* capturePath;
 int captureFrames;
}
- (void)startGame;
- (void)tick:(NSTimer*)timer;
- (void)performAction:(const UIButton&)button;
- (void)captureNow;
- (BOOL)runRenderStress:(int)frames;
@end

@interface CinderlineAccessibilityButton : NSAccessibilityElement {
 @public __weak CinderlineView* owner; UIButton button;
}
@end
@implementation CinderlineAccessibilityButton
- (BOOL)accessibilityPerformPress {if(owner&&button.enabled){[owner performAction:button];return YES;}return NO;}
@end

@implementation CinderlineView
- (instancetype)initWithFrame:(NSRect)frame {
 self=[super initWithFrame:frame]; if(self){
  menu=YES;paused=NO;showHelp=NO;mapChoice=0;placement=Kind::Foundry;keys.fill(false);fps=60;
  [self setWantsLayer:YES];[self setAccessibilityElement:YES];[self setAccessibilityRole:NSAccessibilityGroupRole];
  [self setAccessibilityLabel:@"Cinderline tactical battlefield. Enter begins skirmish. H opens controls. F2 selects army."];
  lastFrame=CACurrentMediaTime();timer=[NSTimer scheduledTimerWithTimeInterval:1./60 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
  for(const auto&e:sim.entities())if(e.team==0&&e.kind==Kind::Headquarters){camera.x=e.pos.x+280;camera.y=e.pos.y+100;break;}
  NSArray* args=[[NSProcessInfo processInfo] arguments];NSUInteger i=[args indexOfObject:@"--capture"];
  if(i!=NSNotFound&&i+1<args.count){capturePath=args[i+1];captureFrames=15;[self startGame];}
  if([args containsObject:@"--play"])[self startGame];
  if([args containsObject:@"--load"])[self loadGame];
  if([args containsObject:@"--capture-menu"])menu=YES;
  if([args containsObject:@"--capture-help"])showHelp=YES;
 }
 return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)viewDidMoveToWindow { [super viewDidMoveToWindow];[[self window] setAcceptsMouseMovedEvents:YES];[[self window] makeFirstResponder:self]; }
- (NSRect)worldRect { return NSMakeRect(0,58,self.bounds.size.width,std::max(100.,self.bounds.size.height-[self hudHeight]-58)); }
- (double)hudHeight { return self.bounds.size.height<560?132:204; }
- (NSRect)minimapRect { double s=[self hudHeight]-30;return NSMakeRect(16,self.bounds.size.height-[self hudHeight]+15,s,s); }
- (NSPoint)eventPoint:(NSEvent*)event { return [self convertPoint:[event locationInWindow] fromView:nil]; }
- (void)message:(std::string)s {if(criticalUntil>CACurrentMediaTime())return;toast=std::move(s);toastUntil=CACurrentMediaTime()+4; }
- (void)home {
 for(const auto&e:sim.entities())if(e.team==0&&e.kind==Kind::Headquarters&&e.alive()){camera.x=e.pos.x+180;camera.y=e.pos.y+80;selected.clear();selected.insert(e.id);break;}
 camera.clamp([self worldRect]);
}
- (void)startGame {
 Config config;config.map=mapChoice;sim.reset(config);for(auto&group:controlGroups)group.clear();observedBuildingHP.clear();criticalUntil=attackAlertCooldown=0;keys.fill(false);menu=NO;paused=NO;showHelp=NO;selected.clear();buildingMode=NO;attackMode=NO;rallyMode=NO;
 camera.zoom=self.bounds.size.height<560?.30:self.bounds.size.width<1000?.47:.68;[self home];
 [self message:"Your Drudges are harvesting. Select one, then build a Kiln to recruit your first army."];
}
- (void)tick:(NSTimer*)unused {
 double now=CACurrentMediaTime(),dt=std::min(.1,now-lastFrame);lastFrame=now;fps=fps*.95+(dt>0?1/dt:60)*.05;
 if(!menu&&!paused&&!showHelp&&sim.winner()<0){
  sim.update((float)dt);
  const Entity* damaged=nullptr;
  for(const auto&e:sim.entities())if(e.team==0&&e.alive()&&definition(e.kind).building){
   auto previous=observedBuildingHP.find(e.id);if(previous!=observedBuildingHP.end()&&e.hp<previous->second-.01f&&(!damaged||e.kind==Kind::Headquarters))damaged=&e;
  }
  observedBuildingHP.clear();for(const auto&e:sim.entities())if(e.team==0&&e.alive()&&definition(e.kind).building)observedBuildingHP[e.id]=e.hp;
  if(damaged&&now>=attackAlertCooldown){toast=std::string(definition(damaged->kind).name)+" UNDER ATTACK — press B to return to your base.";toastUntil=criticalUntil=now+5;attackAlertCooldown=now+8;}
  if(sim.alert()!=lastSimAlert){lastSimAlert=sim.alert();if(sim.time()>2)[self message:lastSimAlert];}
  if(!self.window.isKeyWindow){keys.fill(false);spaceHeld=NO;aTapEligible=NO;}
  double amount=700*dt/camera.zoom;
  if(keys[123]||(keys[0]&&now-aKeyStart>.18))camera.x-=amount; if(keys[124]||keys[2])camera.x+=amount;
  if(keys[126]||keys[13])camera.y-=amount; if(keys[125]||keys[1])camera.y+=amount;
  camera.clamp([self worldRect]);
 }
 for(auto it=selected.begin();it!=selected.end();) {const Entity* e=sim.find(*it);if(!e||!e->alive())it=selected.erase(it);else ++it;}
 [self setNeedsDisplay:YES];
 if(capturePath&&captureFrames>0&&--captureFrames==0)[self captureNow];
}
- (void)captureNow {
 [self displayIfNeeded]; NSBitmapImageRep* bitmap=[self bitmapImageRepForCachingDisplayInRect:self.bounds];
 [self cacheDisplayInRect:self.bounds toBitmapImageRep:bitmap];NSData* png=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
 [png writeToFile:capturePath atomically:YES];fprintf(stdout,"Captured native renderer: %s\n",[capturePath UTF8String]);fflush(stdout);
}
- (void)send:(Command)cmd {
 cmd.team=0;if(cmd.units.empty())for(Id id:selected)cmd.units.push_back(id);
 auto result=sim.command(cmd);[self message:result.message.empty()?(result.accepted?"Order confirmed":"Order unavailable"):result.message];
 if(result.accepted){pulsePoint=cmd.point;commandPulse=CACurrentMediaTime()+.75;}
}
- (const Entity*)primary {
 const Entity* result=nullptr;for(Id id:selected){const Entity* e=sim.find(id);if(e&&(!result||e->id<result->id))result=e;}return result;
}
- (const Entity*)entityAt:(Vec2)p {
 const Entity* closest=nullptr;float distance=1e9;
 for(const auto&e:sim.entities()){
  if(!e.alive()||(e.team!=0&&e.kind!=Kind::Resource&&!sim.visible(0,e.pos)))continue;
  float d=hypotf(e.pos.x-p.x,e.pos.y-p.y),r=std::max(definition(e.kind).radius,12.f/(float)camera.zoom);
  if(d<r*1.35&&d<distance){closest=&e;distance=d;}
 }return closest;
}
- (void)activateWorld:(NSPoint)p event:(NSEvent*)event {
 Vec2 world=camera.world(p,[self worldRect]);const Entity* hit=[self entityAt:world];bool shift=event.modifierFlags&NSEventModifierFlagShift;
 if(buildingMode){Command cmd;cmd.type=CommandType::Build;cmd.kind=placement;cmd.point=world;auto result=sim.canPlace(0,placement,world);[self send:cmd];if(result&&!shift)buildingMode=NO;return;}
 if(attackMode){Command cmd;cmd.type=CommandType::AttackMove;cmd.point=world;[self send:cmd];attackMode=NO;return;}
 if(rallyMode){Command cmd;cmd.type=CommandType::Rally;cmd.point=world;[self send:cmd];rallyMode=NO;return;}
 if(hit&&hit->team==0&&hit->kind!=Kind::Resource){
  if(!shift)selected.clear(); if(shift&&selected.count(hit->id))selected.erase(hit->id);else selected.insert(hit->id);
  if(event.clickCount==2){for(const auto&e:sim.entities())if(e.team==0&&e.kind==hit->kind&&e.alive()&&NSPointInRect(camera.screen(e.pos,[self worldRect]),[self worldRect]))selected.insert(e.id);}
  return;
 }
 if(selected.empty())return;
 Command cmd;cmd.point=world;
 if(hit&&hit->kind==Kind::Resource){cmd.type=CommandType::Gather;cmd.target=hit->id;}
 else if(hit&&hit->team==1){cmd.type=CommandType::Attack;cmd.target=hit->id;}
 else cmd.type=CommandType::Move;
 [self send:cmd];
}
- (void)mouseDown:(NSEvent*)event {
 mouse=down=lastDrag=[self eventPoint:event];
 for(auto it=buttons.rbegin();it!=buttons.rend();++it)if(NSPointInRect(mouse,it->rect)){if(it->enabled)[self performAction:*it];else[self message:"Requirements not met or insufficient ore."];return;}
 if(menu||paused||showHelp||sim.winner()>=0)return;
 if(NSPointInRect(mouse,[self minimapRect])){minimapDrag=YES;[self moveOnMinimap:mouse];return;}
 if(!NSPointInRect(mouse,[self worldRect]))return;
 if(spaceHeld){panning=YES;return;}dragging=YES;
}
- (void)moveOnMinimap:(NSPoint)p {NSRect r=[self minimapRect];camera.x=(p.x-r.origin.x)/r.size.width*Simulation::WorldSize;camera.y=(p.y-r.origin.y)/r.size.height*Simulation::WorldSize;camera.clamp([self worldRect]);}
- (void)mouseDragged:(NSEvent*)event {
 mouse=[self eventPoint:event];
 if(minimapDrag){[self moveOnMinimap:mouse];return;}
 if(panning){camera.x-=(mouse.x-lastDrag.x)/camera.zoom;camera.y-=(mouse.y-lastDrag.y)/(camera.zoom*.72);camera.clamp([self worldRect]);lastDrag=mouse;}
}
- (void)mouseUp:(NSEvent*)event {
 mouse=[self eventPoint:event];if(minimapDrag){minimapDrag=NO;return;}if(panning){panning=NO;return;}
 if(!dragging)return;dragging=NO;
 if(hypot(mouse.x-down.x,mouse.y-down.y)>6&&!buildingMode&&!attackMode&&!rallyMode){
  NSRect rect=dragRect(down,mouse);if(!(event.modifierFlags&NSEventModifierFlagShift))selected.clear();
  for(const auto&e:sim.entities())if(e.team==0&&e.alive()&&!definition(e.kind).building&&NSPointInRect(camera.screen(e.pos,[self worldRect]),rect))selected.insert(e.id);
 }else [self activateWorld:mouse event:event];
}
- (void)mouseMoved:(NSEvent*)event {mouse=[self eventPoint:event];}
- (void)rightMouseDown:(NSEvent*)event {if(menu||paused||showHelp)return;mouse=down=lastDrag=[self eventPoint:event];panning=YES;}
- (void)rightMouseDragged:(NSEvent*)event {[self mouseDragged:event];}
- (void)rightMouseUp:(NSEvent*)event {
 if(menu||paused||showHelp||sim.winner()>=0){panning=NO;return;}
 NSPoint p=[self eventPoint:event];panning=NO;if(hypot(p.x-down.x,p.y-down.y)<5){
  if(buildingMode||attackMode||rallyMode){buildingMode=NO;attackMode=NO;rallyMode=NO;return;}
  if(NSPointInRect(p,[self worldRect])){
   Vec2 world=camera.world(p,[self worldRect]);const Entity* target=[self entityAt:world];Command cmd;cmd.point=world;
   bool workerSelected=false;for(Id id:selected){const Entity* unit=sim.find(id);if(unit&&unit->alive()&&unit->team==0&&unit->kind==Kind::Worker){workerSelected=true;break;}}
   if(target&&target->kind==Kind::Resource){cmd.type=CommandType::Gather;cmd.target=target->id;}
   else if(target&&target->team==0&&definition(target->kind).building&&target->progress<1&&workerSelected){cmd.type=CommandType::ResumeConstruction;cmd.target=target->id;}
   else if(target&&target->team==1){cmd.type=CommandType::Attack;cmd.target=target->id;}else cmd.type=CommandType::Move;[self send:cmd];
  }
 }
}
- (void)otherMouseDown:(NSEvent*)event {mouse=down=lastDrag=[self eventPoint:event];panning=YES;}
- (void)otherMouseDragged:(NSEvent*)event {[self mouseDragged:event];}
- (void)otherMouseUp:(NSEvent*)event {panning=NO;}
- (void)scrollWheel:(NSEvent*)event {
 if(menu||paused||showHelp)return;NSPoint p=[self eventPoint:event];if(!NSPointInRect(p,[self worldRect]))return;
 Vec2 before=camera.world(p,[self worldRect]);double delta=event.scrollingDeltaY*(event.hasPreciseScrollingDeltas?.008:.08);
 camera.zoom=std::clamp(camera.zoom*exp(delta),Camera::minimumZoom([self worldRect]),1.65);Vec2 after=camera.world(p,[self worldRect]);camera.x+=before.x-after.x;camera.y+=before.y-after.y;camera.clamp([self worldRect]);
}
- (void)keyDown:(NSEvent*)event {
 if(event.keyCode<128)keys[event.keyCode]=true;if(event.keyCode==49){spaceHeld=YES;return;}
 NSString* string=[event charactersIgnoringModifiers];if(event.isARepeat)return;
 if(event.keyCode==53){if(buildingMode||attackMode||rallyMode){buildingMode=NO;attackMode=NO;rallyMode=NO;}else if(showHelp)showHelp=NO;else if(!menu)paused=!paused;return;}
 if([string.lowercaseString isEqualToString:@"h"]){showHelp=!showHelp;return;}
 if(menu){if(event.keyCode==36)[self startGame];return;}
 if([string.lowercaseString isEqualToString:@"p"]&&!showHelp){paused=!paused;return;}
 if(paused||showHelp)return;
 if(event.keyCode==120){[self selectArmy];return;} // F2
 if(event.keyCode==115||[string.lowercaseString isEqualToString:@"b"]){[self home];return;}
 if(event.keyCode==96){[self saveGame];return;}if(event.keyCode==101){[self loadGame];return;}
 // A is attack-move unless held for movement; arrows and W/S/D remain available for camera.
 if([string.lowercaseString isEqualToString:@"a"]){aKeyStart=CACurrentMediaTime();aTapEligible=!(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption));if(!aTapEligible)keys[0]=false;return;}
 if([string.lowercaseString isEqualToString:@"x"]){Command cmd;cmd.type=CommandType::Stop;[self send:cmd];return;}
 if([string.lowercaseString isEqualToString:@"v"]){Command cmd;cmd.type=CommandType::Hold;[self send:cmd];return;}
 if([string.lowercaseString isEqualToString:@"r"]){rallyMode=YES;[self message:"RALLY POINT — click the battlefield."];return;}
 if([string.lowercaseString isEqualToString:@"p"]){paused=!paused;return;}
 if(event.keyCode==51){Command cmd;cmd.type=CommandType::CancelQueue;cmd.queueIndex=0;[self send:cmd];return;}
 if(string.length==1){int n=[string intValue];if(n>=1&&n<=9){if(event.modifierFlags&NSEventModifierFlagCommand){controlGroups[n].assign(selected.begin(),selected.end());[self message:"Control group "+std::to_string(n)+" assigned."];return;}if(!controlGroups[n].empty()){selected.clear();for(Id id:controlGroups[n]){const Entity*e=sim.find(id);if(e&&e->alive())selected.insert(id);}[self message:"Control group "+std::to_string(n)+" selected."];return;}int i=0;for(const auto&button:buttons)if((button.action>=Train&&button.action<=Rally)||button.action==ResumeBuild){if(++i==n){[self performAction:button];break;}}}}
}
- (void)flagsChanged:(NSEvent*)event {if(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption)){aTapEligible=NO;keys[0]=false;}}
- (void)keyUp:(NSEvent*)event {if(event.keyCode<128)keys[event.keyCode]=false;if(event.keyCode==49)spaceHeld=NO;if(event.keyCode==0){if(aTapEligible&&!(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption))&&!menu&&!paused&&!showHelp&&CACurrentMediaTime()-aKeyStart<.18){attackMode=YES;[self message:"ATTACK MOVE — click a destination. Right click or Esc cancels."];}aTapEligible=NO;}}
- (void)selectArmy {selected.clear();for(const auto&e:sim.entities())if(e.team==0&&e.alive()&&!definition(e.kind).building&&e.kind!=Kind::Worker)selected.insert(e.id);[self message:"Army selected. Click terrain to move; A then click to attack-move."];}
- (std::string)savePath {
 NSString* root=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Cinderline"];
 [[NSFileManager defaultManager] createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];return [[root stringByAppendingPathComponent:@"skirmish.save"] UTF8String];
}
- (void)saveGame {[self message:sim.save([self savePath])?"Skirmish saved. F9 restores this checkpoint.":"Could not save skirmish."];}
- (void)loadGame {if(sim.load([self savePath])){mapChoice=sim.config().map;for(auto&group:controlGroups)group.clear();observedBuildingHP.clear();criticalUntil=attackAlertCooldown=0;menu=NO;paused=NO;selected.clear();[self home];[self message:"Checkpoint restored."];}else[self message:"No valid checkpoint found. Use F5 to save a skirmish first."];}
- (void)performAction:(const UIButton&)button {
 if(!button.enabled)return;
 Command cmd;const Entity* e=[self primary];
 switch(button.action){
  case Begin:[self startGame];break;case Map0:mapChoice=0;sim.reset(Config{0});[self home];break;case Map1:mapChoice=1;sim.reset(Config{1});[self home];break;case Map2:mapChoice=2;sim.reset(Config{2});[self home];break;
  case Resume:if(button.parameter==1)paused=!paused;else if(button.parameter==2)showHelp=NO;else{paused=NO;showHelp=NO;}break;case Rematch:[self startGame];break;case Help:showHelp=!showHelp;break;
  case QuitMenu:menu=YES;paused=NO;showHelp=NO;break;
  case Train:cmd.type=CommandType::Train;cmd.kind=button.kind;if(e)cmd.units={e->id};[self send:cmd];break;
  case Build:placement=button.kind;buildingMode=YES;attackMode=NO;rallyMode=NO;[self message:std::string("PLACE ")+definition(placement).name+" — click a clear area near your forces. Esc cancels."];break;
  case AttackMode:attackMode=YES;buildingMode=NO;rallyMode=NO;[self message:"ATTACK MOVE — click a destination. Your army will engage enemies along the way."];break;
  case Stop:cmd.type=CommandType::Stop;[self send:cmd];break;
  case Hold:cmd.type=CommandType::Hold;[self send:cmd];break;
  case Research:cmd.type=CommandType::Research;cmd.queueIndex=button.parameter;if(e)cmd.units={e->id};[self send:cmd];break;
  case Cancel:cmd.type=(e&&e->progress<1)?CommandType::CancelBuilding:CommandType::CancelQueue;cmd.queueIndex=0;if(e)cmd.units={e->id};[self send:cmd];break;
  case ResumeBuild:
   if(menu||paused||showHelp||sim.winner()>=0||!e||e->team!=0||!definition(e->kind).building||e->progress>=1)break;
   cmd.type=CommandType::ResumeConstruction;cmd.target=e->id;cmd.point=e->pos;
   // The shared command chooses the nearest available Drudge from these candidates.
   for(const Entity& worker:sim.entities())if(worker.alive()&&worker.team==0&&worker.kind==Kind::Worker&&worker.order!=Order::Construct)cmd.units.push_back(worker.id);
   if(cmd.units.empty())[self message:"No available Drudge. Stop a builder or train another at your Anchor."];else[self send:cmd];
   break;
  case Rally:rallyMode=YES;buildingMode=NO;attackMode=NO;[self message:"RALLY POINT — click where newly recruited units should assemble."];break;
  case Army:[self selectArmy];break;case Home:[self home];break;case Save:[self saveGame];break;case Load:[self loadGame];break;
  case FilterType:filterSelection(selected,sim,button.kind);subgroupPage=0;break;
  case GroupPage:subgroupPage=(subgroupPage+1)%std::max(1,button.parameter);break;
 }
 [self setNeedsDisplay:YES];
}

- (void)drawButton:(UIButton)button context:(CGContextRef)c accent:(BOOL)accent {
 bool hover=NSPointInRect(mouse,button.rect);Color bg=accent?Color{.11,.36,.34,1}:Color{.065,.10,.14,1};
 if(hover&&button.enabled)bg=accent?Color{.15,.47,.43,1}:Color{.10,.18,.22,1};if(!button.enabled)bg=Color{.05,.075,.10,1};
 box(c,button.rect,bg);borderRect(c,button.rect,accent?Cyan:(hover?Muted:Edge));
 double font=button.rect.size.height<39?11:12;double ty=button.rect.origin.y+(button.detail.empty()?(button.rect.size.height-15)/2:7);
 label(button.label,NSMakeRect(button.rect.origin.x+10,ty,button.rect.size.width-18,20),font,button.enabled?(accent?Cyan:Ink):Muted,true);
 if(!button.detail.empty())label(button.detail,NSMakeRect(button.rect.origin.x+10,button.rect.origin.y+25,button.rect.size.width-18,16),10,button.enabled?Muted:Color{.28,.35,.40,1},false,true);
 buttons.push_back(button);
}

- (void)drawTerrain:(CGContextRef)c rect:(NSRect)world {
 box(c,world,Navy);CGContextSaveGState(c);CGContextClipToRect(c,NSRectToCGRect(world));
 Vec2 tl=camera.world(world.origin,world),br=camera.world(NSMakePoint(NSMaxX(world),NSMaxY(world)),world);
 const float cell=Simulation::WorldSize/Simulation::FogSize;
 int x0=std::max(0,(int)(tl.x/cell)-1),y0=std::max(0,(int)(tl.y/cell)-1),x1=std::min(63,(int)(br.x/cell)+1),y1=std::min(63,(int)(br.y/cell)+1);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
  Vec2 p{(x+.5f)*cell,(y+.5f)*cell};bool v=sim.visible(0,p),seen=sim.explored(0,p);float n=hash(x,y);
  Color col=v?Color{.069+n*.023,.102+n*.027,.119+n*.022,1}:(seen?Color{.039+n*.01,.058+n*.013,.075+n*.014,1}:Color{.021+n*.004,.030+n*.005,.047+n*.005,1});
  NSPoint a=camera.screen({x*cell,y*cell},world);double w=cell*camera.zoom,h=w*.72;box(c,NSMakeRect(a.x,a.y,w+1,h+1),col);
  if(v&&n>.56){line(c,NSMakePoint(a.x+w*.18,a.y+h*.67),NSMakePoint(a.x+w*.62,a.y+h*.58),Color{.11,.15,.16,.55});}
  if(x%4==0)line(c,a,NSMakePoint(a.x,a.y+h),Color{.20,.30,.31,v?.09:.035});
  if(y%4==0)line(c,a,NSMakePoint(a.x+w,a.y),Color{.20,.30,.31,v?.09:.035});
 }
 // Faint survey rings around the two starting bases make navigation legible.
 for(const auto&e:sim.entities())if(e.kind==Kind::Headquarters&&sim.explored(0,e.pos)){
  NSPoint p=camera.screen(e.pos,world);double r=220*camera.zoom;
  ring(c,NSMakeRect(p.x-r,p.y-r*.72,r*2,r*1.44),Color{.24,.47,.47,.15});
  for(int a=0;a<4;a++){double angle=a*Pi/2;line(c,NSMakePoint(p.x+cos(angle)*r*.92,p.y+sin(angle)*r*.72*.92),NSMakePoint(p.x+cos(angle)*r*1.08,p.y+sin(angle)*r*.72*1.08),Color{.27,.51,.49,.3});}
 }
 for(const auto&o:sim.obstacles()){
  if(!sim.explored(0,o.center))continue;NSPoint p=camera.screen(o.center,world);double w=o.half.x*camera.zoom,h=o.half.y*camera.zoom*.72,z=18*camera.zoom;
  Color stone=sim.visible(0,o.center)?Color{.20,.24,.25,1}:Color{.095,.125,.145,1};
  oval(c,NSMakeRect(p.x-w-4,p.y-h+8,w*2+14,h*2+8),Color{0,0,0,.28});
  poly(c,{{p.x-w,p.y-h},{p.x+w*.65,p.y-h-z},{p.x+w,p.y+h*.6},{p.x-w*.7,p.y+h}},Color{stone.r*.6,stone.g*.65,stone.b*.7,1});
  poly(c,{{p.x-w,p.y-h},{p.x-w*.6,p.y-h-z},{p.x+w*.65,p.y-h-z},{p.x+w,p.y+h*.6-z},{p.x-w*.7,p.y+h-z}},stone,Color{.30,.35,.36,.45});
  line(c,NSMakePoint(p.x-w*.5,p.y-h-z),NSMakePoint(p.x+w*.12,p.y+h*.2-z),Color{.08,.11,.13,.7},1.5);
 }
 NSPoint origin=camera.screen({0,0},world),far=camera.screen({Simulation::WorldSize,Simulation::WorldSize},world);
 borderRect(c,NSMakeRect(origin.x,origin.y,far.x-origin.x,far.y-origin.y),Color{.18,.30,.35,.65},2);
 CGContextRestoreGState(c);
}
- (void)drawEntity:(const Entity&)e context:(CGContextRef)c rect:(NSRect)world ghost:(BOOL)ghost {
 const auto&def=definition(e.kind);NSPoint p=camera.screen(e.pos,world);bool seen=sim.visible(0,e.pos);
 if(!ghost&&!seen&&e.team!=0&&e.kind!=Kind::Resource)return;
 if(!ghost&&e.kind==Kind::Resource&&!sim.explored(0,e.pos))return;
 if(!NSPointInRect(p,NSInsetRect(world,-140,-140)))return;
 double z=camera.zoom,rad=std::max(5.,def.radius*z);Color team=e.team==0?Cyan:Coral;
 if(!seen&&e.kind==Kind::Resource)team=Muted;
 if(ghost){CGContextSaveGState(c);CGContextSetAlpha(c,.6);}
 bool sel=selected.count(e.id)&&!ghost;
 oval(c,NSMakeRect(p.x-rad*1.18+4*z,p.y-rad*.58+8*z,rad*2.36,rad*1.16),Color{0,0,0,.36});
 if(sel){ring(c,NSMakeRect(p.x-rad*1.3,p.y-rad*.9,rad*2.6,rad*1.8),team,1.6);ring(c,NSMakeRect(p.x-rad*1.45,p.y-rad*1.0,rad*2.9,rad*2.),Color{team.r,team.g,team.b,.19},3);}
 if(e.kind==Kind::Resource){
  for(int i=0;i<5;i++){double xx=p.x+(i-2)*rad*.36,yy=p.y+(i%2)*rad*.24,hh=rad*(.7+hash(e.id,i)*.8);
   poly(c,{{xx-rad*.2,yy},{xx-rad*.24,yy-hh*.65},{xx,yy-hh},{xx+rad*.2,yy-hh*.55},{xx+rad*.16,yy}},seen?Color{.20,.69,.77,1}:Color{.13,.28,.34,1},seen?Color{.48,.90,.91,.7}:Muted);
   line(c,NSMakePoint(xx,yy-hh),NSMakePoint(xx+rad*.02,yy-rad*.1),Color{.62,1,.98,seen?.7:.13});
  }
  if(sel||hypot(mouse.x-p.x,mouse.y-p.y)<rad*2)label(std::to_string((int)e.resource)+" ORE",NSMakeRect(p.x-45,p.y+rad*.7,90,18),9,Cyan,false,true,NSTextAlignmentCenter);
 }else if(def.building){
  double w=rad*1.1,h=rad*.68,t=rad*.55;Color metal=e.team==0?Color{.15,.27,.29,1}:Color{.31,.19,.19,1};
  poly(c,{{p.x-w,p.y-h},{p.x+w,p.y-h},{p.x+w,p.y+h},{p.x-w,p.y+h}},Color{.075,.105,.12,1},Color{.25,.35,.37,1});
  poly(c,{{p.x-w*.86,p.y-h-t},{p.x+w*.86,p.y-h-t},{p.x+w,p.y-h*.1-t},{p.x+w*.86,p.y+h-t},{p.x-w*.86,p.y+h-t},{p.x-w,p.y-h*.1-t}},metal,Color{team.r*.6,team.g*.6,team.b*.6,1});
  poly(c,{{p.x-w*.86,p.y+h-t},{p.x+w*.86,p.y+h-t},{p.x+w*.86,p.y+h},{p.x-w*.86,p.y+h}},Color{metal.r*.5,metal.g*.5,metal.b*.5,1},Edge);
  line(c,NSMakePoint(p.x-w*.72,p.y+h-t+3*z),NSMakePoint(p.x+w*.72,p.y+h-t+3*z),team,2*z);
  if(e.kind==Kind::Headquarters){
   double r=rad*.5;poly(c,{{p.x-r,p.y-t-r*.3},{p.x,p.y-t-r*.8},{p.x+r,p.y-t-r*.3},{p.x+r*.72,p.y-t+r*.35},{p.x-r*.72,p.y-t+r*.35}},Color{.20,.36,.37,1},team,1.2);
   ring(c,NSMakeRect(p.x-r*.38,p.y-t-r*.45,r*.76,r*.55),team,2*z);
   for(int i=-1;i<=1;i+=2){box(c,NSMakeRect(p.x+i*w*.72-4*z,p.y-t-5*z,8*z,10*z),team);}
   line(c,NSMakePoint(p.x+w*.55,p.y-t-h*.4),NSMakePoint(p.x+w*.55,p.y-t-h-18*z),Muted,2*z);oval(c,NSMakeRect(p.x+w*.55-3*z,p.y-t-h-21*z,6*z,6*z),team);
  }else if(e.kind==Kind::Foundry||e.kind==Kind::MotorPool){
   for(int i=0;i<3;i++){box(c,NSMakeRect(p.x-w*.6+i*w*.45,p.y-t-h*.58,w*.30,h*.65),Color{.04,.07,.09,1});line(c,NSMakePoint(p.x-w*.59+i*w*.45,p.y-t-h*.58),NSMakePoint(p.x-w*.34+i*w*.45,p.y-t-h*.58),team,2*z);}
   if(e.kind==Kind::MotorPool){ring(c,NSMakeRect(p.x-w*.35,p.y-t+h*.1,w*.7,h*.4),Amber,2*z);}
  }else if(e.kind==Kind::Laboratory){
   oval(c,NSMakeRect(p.x-rad*.43,p.y-t-rad*.56,rad*.86,rad*.65),Color{.09,.30,.35,1});ring(c,NSMakeRect(p.x-rad*.43,p.y-t-rad*.56,rad*.86,rad*.65),team,2*z);line(c,NSMakePoint(p.x,p.y-t-rad*.7),NSMakePoint(p.x,p.y-t+rad*.2),team,1.5*z);
  }else if(e.kind==Kind::Turret){
   oval(c,NSMakeRect(p.x-rad*.55,p.y-t-rad*.35,rad*1.1,rad*.7),metal);double a=e.facing;line(c,NSMakePoint(p.x,p.y-t),NSMakePoint(p.x+cos(a)*rad*1.3,p.y-t+sin(a)*rad*.85),team,5*z);
  }else if(e.kind==Kind::Processor){
   for(int i=-1;i<=1;i++){double xx=p.x+i*rad*.45;box(c,NSMakeRect(xx-rad*.13,p.y-t-rad*.6,rad*.26,rad*.8),Color{.10,.18,.21,1});box(c,NSMakeRect(xx-rad*.10,p.y-t-rad*.53,rad*.20,rad*.12),Amber);}
  }
  if(e.progress<1){box(c,NSMakeRect(p.x-w,p.y+h+5,w*2,4),Navy);box(c,NSMakeRect(p.x-w,p.y+h+5,w*2*e.progress,4),Amber);}
 }else{
  CGContextSaveGState(c);CGContextTranslateCTM(c,p.x,p.y-(def.air?rad*.9:0));CGContextScaleCTM(c,1,.8);CGContextRotateCTM(c,e.facing+Pi/2);
  double r=rad;Color armor=e.team==0?Color{.27,.40,.41,1}:Color{.46,.27,.25,1};
  if(e.kind==Kind::Worker){
   poly(c,{{-r*.65,r*.5},{-r*.65,-r*.4},{0,-r*.9},{r*.65,-r*.4},{r*.65,r*.5}},armor,team);
   line(c,NSMakePoint(-r*.72,-r*.1),NSMakePoint(-r*.95,-r*.85),Muted,3*z);line(c,NSMakePoint(r*.72,-r*.1),NSMakePoint(r*.95,-r*.85),Muted,3*z);
   box(c,NSMakeRect(-r*.3,-r*.35,r*.6,r*.34),e.carried>0?Amber:team);
  }else if(e.kind==Kind::Striker||e.kind==Kind::Lancer||e.kind==Kind::Mender){
   line(c,NSMakePoint(-r*.32,r*.25),NSMakePoint(-r*.53,r*.85),armor,4*z);line(c,NSMakePoint(r*.32,r*.25),NSMakePoint(r*.53,r*.85),armor,4*z);
   poly(c,{{-r*.65,-r*.25},{0,-r*.67},{r*.65,-r*.25},{r*.45,r*.42},{-r*.45,r*.42}},armor,team);
   oval(c,NSMakeRect(-r*.25,-r*.70,r*.5,r*.45),team);
   if(e.kind==Kind::Mender){line(c,NSMakePoint(-r*.25,0),NSMakePoint(r*.25,0),Ink,2*z);line(c,NSMakePoint(0,-r*.25),NSMakePoint(0,r*.25),Ink,2*z);}
   else line(c,NSMakePoint(r*.52,-r*.1),NSMakePoint(r*.50,e.kind==Kind::Lancer?-r*1.55:-r*.90),e.kind==Kind::Lancer?Amber:Ink,2.5*z);
  }else if(e.kind==Kind::Scout){
   poly(c,{{0,-r*1.1},{r*.58,r*.45},{0,r*.1},{-r*.58,r*.45}},armor,team);line(c,NSMakePoint(-r*.76,0),NSMakePoint(-r*.76,r*.55),Muted,3*z);line(c,NSMakePoint(r*.76,0),NSMakePoint(r*.76,r*.55),Muted,3*z);
  }else if(e.kind==Kind::Kite){
   poly(c,{{0,-r*1.3},{r*1.25,r*.6},{r*.4,r*.4},{0,r*.92},{-r*.4,r*.4},{-r*1.25,r*.6}},armor,team);poly(c,{{0,-r*.55},{r*.25,r*.1},{-r*.25,r*.1}},team);
   oval(c,NSMakeRect(-r*.57,r*.3,r*.2,r*.4),Cyan);oval(c,NSMakeRect(r*.37,r*.3,r*.2,r*.4),Cyan);
  }else{
   for(int i=-1;i<=1;i+=2)box(c,NSMakeRect(i*r*.73-r*.21,-r*.7,r*.42,r*1.5),Color{.11,.15,.16,1});
   poly(c,{{-r*.63,-r*.73},{r*.63,-r*.73},{r*.76,r*.64},{-r*.76,r*.64}},armor,team);
   ring(c,NSMakeRect(-r*.4,-r*.33,r*.8,r*.72),team,1.5*z);line(c,NSMakePoint(0,0),NSMakePoint(0,e.kind==Kind::Mortar?-r*1.5:-r*1.05),Ink,e.kind==Kind::Mortar?5*z:3*z);
  }
  CGContextRestoreGState(c);
 }
 if(e.kind!=Kind::Resource&&!ghost&&(sel||e.hp<def.hp||e.team==1)){
  double width=std::max(20.,rad*1.8),yy=p.y-rad*(def.building?1.65:1.6)-8;
  box(c,NSMakeRect(p.x-width/2,yy,width,3),Color{0,0,0,.8});box(c,NSMakeRect(p.x-width/2,yy,width*std::clamp(e.hp/def.hp,0.f,1.f),3),team);
 }
 if(ghost)CGContextRestoreGState(c);
}

- (void)drawBattlefield:(CGContextRef)c rect:(NSRect)world {
 [self drawTerrain:c rect:world];CGContextSaveGState(c);CGContextClipToRect(c,NSRectToCGRect(world));
 std::vector<const Entity*> ordered;for(const auto&e:sim.entities())if(e.alive())ordered.push_back(&e);
 std::sort(ordered.begin(),ordered.end(),[](const Entity*a,const Entity*b){return a->pos.y<b->pos.y;});
 for(const auto*e:ordered)[self drawEntity:*e context:c rect:world ghost:NO];
 // Effect links require the core's recorded/current endpoint and segment gate.
 // Local marks are withheld if any occupied fog cell is hidden.
 struct EffectPoint {double x,y,z;};
 const double effectScale=self.bounds.size.height<560?.72:1.0,strokeScale=std::clamp(camera.zoom,.70,1.25);
 auto withAlpha=[](Color color,double alpha){color.a=alpha;return color;};
 auto effectHeight=[](Kind kind){const auto&d=definition(kind);return d.radius*(d.air?.95:d.building?.60:.30);};
 auto effectScreen=[&](EffectPoint p){NSPoint q=camera.screen({(float)p.x,(float)p.y},world);q.y-=p.z*camera.zoom;return q;};
 auto areaVisible=[&](EffectPoint p,double radius){
  constexpr double cell=Simulation::WorldSize/Simulation::FogSize;
  if(p.x-radius<0||p.y-radius<0||p.x+radius>=Simulation::WorldSize||p.y+radius>=Simulation::WorldSize)return false;
  int x0=(int)floor((p.x-radius)/cell),x1=(int)floor((p.x+radius)/cell),y0=(int)floor((p.y-radius)/cell),y1=(int)floor((p.y+radius)/cell);
  for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)if(!sim.visible(0,{(float)((x+.5)*cell),(float)((y+.5)*cell)}))return false;
  return true;
 };
 auto effectLine=[&](EffectPoint a,EffectPoint b,Color color,double width){line(c,effectScreen(a),effectScreen(b),color,width*strokeScale);};
 auto effectRing=[&](EffectPoint p,double radius,Color color,double width){if(!areaVisible(p,radius))return;NSPoint q=effectScreen(p);double r=radius*camera.zoom;ring(c,NSMakeRect(q.x-r,q.y-r*.72,r*2,r*1.44),color,width*strokeScale);};
 auto effectCross=[&](EffectPoint p,double radius,Color color,double width){if(!areaVisible(p,radius*2))return;NSPoint q=effectScreen(p);double r=std::clamp(radius*camera.zoom,2.5,7.0)*effectScale;line(c,NSMakePoint(q.x-r,q.y),NSMakePoint(q.x+r,q.y),color,width*strokeScale);line(c,NSMakePoint(q.x,q.y-r),NSMakePoint(q.x,q.y+r),color,width*strokeScale);};
 for(const auto&fx:sim.effects()){
  if(fx.life<=0||fx.duration<=0)continue;
  bool sourceVisible=sim.effectVisible(fx,0,true),targetVisible=sim.effectVisible(fx,0,false);if(!sourceVisible&&!targetVisible)continue;
  double age=std::clamp(1.0-fx.life/fx.duration,0.0,1.0),fade=(1-age)*(1-age*.4),phase=(fx.id%31)*.37;
  Color shot=fx.team==0?Cyan:Coral;EffectPoint from{fx.from.x,fx.from.y,effectHeight(fx.sourceKind)},to{fx.to.x,fx.to.y,effectHeight(fx.targetKind)};
  auto point=[&](double t){return EffectPoint{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};};
  if(fx.type==EffectType::Weapon){
   bool heavy=fx.sourceKind==Kind::Bastion||fx.sourceKind==Kind::Turret;
   if(sourceVisible&&age<.30){double flash=std::max(0.0,1-age*3.5);effectCross(from,(heavy?13:8)*effectScale,withAlpha(shot,flash),heavy?2.2:1.5);if(heavy)effectRing(from,(8+age*19)*effectScale,withAlpha(shot,flash*.6),1.2);}
   if(!sim.effectLinkVisible(fx,0))continue;
   double head=std::clamp(age/.78,0.0,1.0);
   if(fx.sourceKind==Kind::Lancer){
    effectLine(from,to,withAlpha(shot,fade*.18),4.2*effectScale);effectLine(from,to,withAlpha(shot,fade),1.5);effectLine(point(std::max(0.0,head-.09)),point(head),withAlpha(Ink,fade),1.0);
   }else if(fx.sourceKind==Kind::Mortar){
    // Arc is visual only: damage timing remains in the shared simulation.
    double rise=std::clamp(hypot(to.x-from.x,to.y-from.y)*.22,70.0,170.0)*effectScale;
    auto arc=[&](double t){EffectPoint p=point(t);p.z+=sin(t*Pi)*rise;return p;};double tail=std::max(0.0,head-.20);
    for(int i=0;i<6;i++)effectLine(arc(tail+(head-tail)*i/6),arc(tail+(head-tail)*(i+1)/6),withAlpha(Amber,fade*(.20+i*.12)),2.2);
    effectCross(arc(head),8*effectScale,withAlpha(Ink,fade),1.6);
   }else{
    double length=heavy?.14:fx.sourceKind==Kind::Scout?.055:.09,width=heavy?2.8:fx.sourceKind==Kind::Worker?1.0:1.7;
    if(heavy)effectLine(point(std::max(0.0,head-length)),point(head),withAlpha(shot,fade*.18),5*effectScale);
    effectLine(point(std::max(0.0,head-length)),point(head),withAlpha(shot,fade),width);
    if(fx.sourceKind==Kind::Kite||fx.sourceKind==Kind::Striker){double second=std::max(0.0,head-(fx.sourceKind==Kind::Kite?.20:.12));effectLine(point(std::max(0.0,second-length*.6)),point(second),withAlpha(shot,fade*.6),1.2);}
   }
  }else if(fx.type==EffectType::Heal){
   Color green{.20,1,.64,1};double pulse=.75+.25*sin(age*Pi*2);
   if(sourceVisible)effectRing(from,(9+age*12)*effectScale,withAlpha(green,fade*.6),1.2);
   if(sim.effectLinkVisible(fx,0)){effectLine(from,to,withAlpha(green,fade*.22),3*effectScale);effectLine(from,to,withAlpha(green,fade*.8),1.2);double head=std::clamp(age/.85,0.0,1.0);effectLine(point(std::max(0.0,head-.07)),point(head),withAlpha(Ink,fade),1.8);}
   if(targetVisible){EffectPoint plus=to;plus.z+=10;effectCross(plus,(10+pulse*5)*effectScale,withAlpha(green,fade*pulse),2);}
  }else if(fx.type==EffectType::Impact&&targetVisible){
   // Generic target-local sparks do not expose a hidden source's kind/team.
   Color spark{1,.82,.50,1};double radius=(4+age*20)*effectScale;effectRing(to,radius,withAlpha(spark,fade*.75),1.4);
   if(areaVisible(to,radius*1.4))for(int i=0;i<4;i++){double angle=phase+i*Pi/2;EffectPoint a{to.x+cos(angle)*radius*.65,to.y+sin(angle)*radius*.65,to.z+radius*.26},b{to.x+cos(angle)*radius*1.35,to.y+sin(angle)*radius*1.35,to.z+radius*.54};effectLine(a,b,withAlpha(spark,fade),1.4);}
  }else if(fx.type==EffectType::Death&&targetVisible){
   const auto&victim=definition(fx.targetKind);EffectPoint center{fx.to.x,fx.to.y,victim.air?effectHeight(fx.targetKind):2};
   double footprint=std::clamp(victim.radius*(victim.building?1.05:1.35),25.0,135.0)*effectScale,radius=footprint*(.2+age*.9);Color fire{1,.52,.22,1};
   effectRing(center,radius,withAlpha(fire,fade),victim.building?2.1:1.5);EffectPoint inner=center;inner.z+=6;effectRing(inner,radius*.58,withAlpha(Amber,fade*.55),1.0);
   if(areaVisible(center,footprint*1.25))for(int i=0;i<(victim.building?6:4);i++){double angle=phase+i*Pi*2/(victim.building?6:4),lift=sin(age*Pi)*(victim.building?34:18);EffectPoint a{center.x+cos(angle)*radius*.8,center.y+sin(angle)*radius*.8,center.z+lift*.8},b{center.x+cos(angle)*radius,center.y+sin(angle)*radius,center.z+lift};effectLine(a,b,withAlpha(fire,fade),2*effectScale);}
  }
 }
 for(Id id:selected){const Entity*e=sim.find(id);if(!e)continue;if(e->order==Order::Move||e->order==Order::AttackMove||e->order==Order::Gather){NSPoint a=camera.screen(e->pos,world),b=camera.screen(e->goal,world);CGFloat dash[]={3,6};CGContextSetLineDash(c,0,dash,2);line(c,a,b,Color{.27,.91,.83,.22});CGContextSetLineDash(c,0,nullptr,0);}if(definition(e->kind).building&&e->rally.x>0){NSPoint b=camera.screen(e->rally,world);line(c,b,NSMakePoint(b.x,b.y-23),Cyan);poly(c,{{b.x,b.y-23},{b.x+15,b.y-18},{b.x,b.y-13}},Cyan);}}
 if(commandPulse>CACurrentMediaTime()){
  NSPoint p=camera.screen(pulsePoint,world);double life=commandPulse-CACurrentMediaTime(),r=12+(1-life/.75)*25;
  ring(c,NSMakeRect(p.x-r,p.y-r*.65,r*2,r*1.3),Color{Cyan.r,Cyan.g,Cyan.b,life/.75},1.5);
  line(c,NSMakePoint(p.x-5,p.y),NSMakePoint(p.x+5,p.y),Cyan);line(c,NSMakePoint(p.x,p.y-5),NSMakePoint(p.x,p.y+5),Cyan);
 }
 if(buildingMode&&NSPointInRect(mouse,world)){
  Vec2 position=camera.world(mouse,world);std::string reason;bool valid=sim.canPlace(0,placement,position,&reason);Entity preview;preview.kind=placement;preview.pos=position;preview.team=0;preview.hp=definition(placement).hp;
  [self drawEntity:preview context:c rect:world ghost:YES];double r=definition(placement).radius*camera.zoom;
  borderRect(c,NSMakeRect(mouse.x-r,mouse.y-r*.72,r*2,r*1.44),valid?Cyan:Coral,2);
  label(valid?"CLICK TO BUILD":reason,NSMakeRect(mouse.x-110,mouse.y+r+10,220,22),11,valid?Cyan:Coral,true,false,NSTextAlignmentCenter);
 }
 if(dragging&&!panning&&hypot(mouse.x-down.x,mouse.y-down.y)>6){NSRect r=dragRect(down,mouse);box(c,r,Color{.22,.91,.84,.07});borderRect(c,r,Cyan);}
 CGContextRestoreGState(c);
}

- (void)drawMinimap:(CGContextRef)c {
 NSRect r=[self minimapRect];box(c,r,Navy);double scale=r.size.width/Simulation::WorldSize,cell=r.size.width/64;
 for(int y=0;y<64;y++)for(int x=0;x<64;x++){Vec2 p{(x+.5f)*75,(y+.5f)*75};if(sim.explored(0,p))box(c,NSMakeRect(r.origin.x+x*cell,r.origin.y+y*cell,cell+1,cell+1),sim.visible(0,p)?Color{.13,.20,.23,1}:Color{.06,.10,.13,1});}
 for(const auto&o:sim.obstacles())if(sim.explored(0,o.center))box(c,NSMakeRect(r.origin.x+(o.center.x-o.half.x)*scale,r.origin.y+(o.center.y-o.half.y)*scale,o.half.x*2*scale,o.half.y*2*scale),Color{.23,.29,.29,1});
 for(const auto&e:sim.entities()){if(!e.alive()||(!sim.visible(0,e.pos)&&e.team!=0&&!(e.kind==Kind::Resource&&sim.explored(0,e.pos))))continue;double size=definition(e.kind).building?4:2;box(c,NSMakeRect(r.origin.x+e.pos.x*scale-size/2,r.origin.y+e.pos.y*scale-size/2,size,size),e.kind==Kind::Resource?Color{.37,.61,.77,1}:e.team==0?Cyan:Coral);}
 Vec2 a=camera.world([self worldRect].origin,[self worldRect]),b=camera.world(NSMakePoint(NSMaxX([self worldRect]),NSMaxY([self worldRect])),[self worldRect]);
 CGContextSaveGState(c);CGContextClipToRect(c,NSRectToCGRect(r));borderRect(c,NSMakeRect(r.origin.x+a.x*scale,r.origin.y+a.y*scale,(b.x-a.x)*scale,(b.y-a.y)*scale),Color{.87,.96,.96,.8});CGContextRestoreGState(c);borderRect(c,r,Edge);
 label("SECTOR "+std::string(mapChoice==0?"01 / RIFT":mapChoice==1?"02 / BASIN":"03 / REACH"),NSMakeRect(r.origin.x+5,r.origin.y+5,r.size.width-8,13),8,Ink,true,true);
}
- (void)drawHUD:(CGContextRef)c {
 double W=self.bounds.size.width,H=self.bounds.size.height,hh=[self hudHeight],y=H-hh;bool small=H<560;NSRect world=[self worldRect];
 box(c,NSMakeRect(0,0,W,58),Panel);line(c,NSMakePoint(0,57),NSMakePoint(W,57),Edge);
 poly(c,{{20,18},{27,12},{34,18},{27,36}},Cyan);poly(c,{{34,18},{41,12},{48,18},{41,36}},Color{.18,.49,.49,1});
 label("CINDERLINE",NSMakeRect(60,13,170,23),17,Ink,true);label("SKIRMISH / "+std::string(mapChoice==0?"SHATTERED RIFT":mapChoice==1?"GLASS BASIN":"IRON REACH"),NSMakeRect(61,36,260,12),8,Muted,false,true);
 double resourceX=W<1000?270:W*.43;
 label("◈  "+std::to_string(sim.players()[0].ore),NSMakeRect(resourceX,17,115,25),18,Cyan,true,true);
 label("SUPPLY  "+std::to_string(sim.supply(0))+" / "+std::to_string(sim.capacity(0)),NSMakeRect(resourceX+115,22,145,20),11,Ink,false,true);
 label("T"+std::to_string(sim.players()[0].tier),NSMakeRect(resourceX+255,20,36,20),13,Amber,true,true);
 label(clockText(sim.time()),NSMakeRect(W-210,21,65,20),W<950?10:12,Muted,false,true);
 [self drawButton:{NSMakeRect(W-128,13,50,31),"?","",Help} context:c accent:NO];
 [self drawButton:{NSMakeRect(W-68,13,52,31),"II","",Resume} context:c accent:NO];buttons.back().action=Resume;
 // Pause is a special header action because Resume also serves the overlay.
 buttons.back().parameter=1;
 box(c,NSMakeRect(0,y,W,hh),Panel);line(c,NSMakePoint(0,y),NSMakePoint(W,y),Edge);
 [self drawMinimap:c];NSRect mini=[self minimapRect];double sx=NSMaxX(mini)+20;
 double actionW=std::min(505.,W*.48),actionX=W-actionW-16,selectionW=std::max(120.,actionX-sx-22);
 const Entity*e=[self primary];
 if(e){
  auto&d=definition(e->kind);label(selected.size()>1?std::to_string(selected.size())+" UNITS SELECTED":d.name,NSMakeRect(sx,y+15,selectionW,25),small?14:19,Ink,true);
  label(selected.size()>1?"MIXED TASK FORCE":d.role,NSMakeRect(sx,y+43,selectionW,small?20:32),small?9:11,Muted,false,true);
  if(selected.size()>1){
   std::array<int,15> counts{};float currentHP=0,maxHP=0;
   for(Id id:selected){const Entity*unit=sim.find(id);if(unit&&unit->alive()){++counts[(int)unit->kind];currentHP+=unit->hp;maxHP+=definition(unit->kind).hp;}}
   std::vector<Kind> kinds;for(int i=0;i<15;i++)if(counts[i]>0)kinds.push_back((Kind)i);
   const int pageSize=kinds.size()>9?8:9,pages=std::max(1,((int)kinds.size()+pageSize-1)/pageSize);subgroupPage%=pages;
   NSRect area=NSMakeRect(sx,y+(small?66:72),selectionW,small?60:90);int chip=0;
   for(int i=subgroupPage*pageSize;i<std::min((int)kinds.size(),(subgroupPage+1)*pageSize);i++,chip++){
    Kind kind=kinds[i];[self drawButton:{subgroupRect(area,chip),std::string(definition(kind).name)+" ×"+std::to_string(counts[(int)kind]),"",FilterType,kind} context:c accent:NO];
   }
   if(pages>1)[self drawButton:{subgroupRect(area,chip),"More  "+std::to_string(subgroupPage+1)+"/"+std::to_string(pages),"",GroupPage,Kind::Worker,pages} context:c accent:NO];
   if(!small)label(std::to_string((int)currentHP)+" / "+std::to_string((int)maxHP)+" TOTAL HP  ·  Click a type to filter",NSMakeRect(sx,y+176,selectionW,17),10,Cyan,false,true);
  }else if(!small){
   label("INTEGRITY",NSMakeRect(sx,y+83,selectionW,13),8,Muted,true,true);box(c,NSMakeRect(sx,y+101,selectionW,5),Navy);box(c,NSMakeRect(sx,y+101,selectionW*std::clamp(e->hp/d.hp,0.f,1.f),5),Cyan);
   label(std::to_string((int)e->hp)+" / "+std::to_string((int)d.hp)+"     "+std::to_string((int)d.damage)+" DMG",NSMakeRect(sx,y+115,selectionW,16),10,Ink,false,true);
   if(d.building&&e->progress<1){
    label(constructionStatus(sim,e->id),NSMakeRect(sx,y+145,selectionW,16),9,Amber,true,true);
    box(c,NSMakeRect(sx,y+165,selectionW,4),Navy);box(c,NSMakeRect(sx,y+165,selectionW*e->progress,4),Amber);
    label(std::to_string((int)(e->progress*100))+"% complete",NSMakeRect(sx,y+177,selectionW,15),9,Muted,false,true);
   }else if(!e->queue.empty()){
    const auto&q=e->queue.front();label(q.research?"RESEARCH IN PROGRESS":std::string("ASSEMBLING ")+definition(q.kind).name,NSMakeRect(sx,y+145,selectionW,16),9,Amber,true,true);
    box(c,NSMakeRect(sx,y+165,selectionW,4),Navy);box(c,NSMakeRect(sx,y+165,selectionW*(1-q.remaining/std::max(.01f,q.total)),4),Amber);
    label(std::to_string(e->queue.size())+" queued · "+std::to_string((int)ceil(q.remaining))+"s remaining",NSMakeRect(sx,y+177,selectionW,15),9,Muted,false,true);
   }else label(d.building?"Set rally point for new units":"Click terrain to move · A to attack-move",NSMakeRect(sx,y+151,selectionW,30),10,Muted);
  }else if(d.building&&e->progress<1){
   label(constructionStatus(sim,e->id),NSMakeRect(sx,y+72,selectionW,20),9,Amber,true,true);
   label(std::to_string((int)(e->progress*100))+"% complete",NSMakeRect(sx,y+96,selectionW,18),9,Muted,false,true);
  }else label(std::to_string((int)e->hp)+" HP   "+std::to_string(e->queue.size())+" QUEUED",NSMakeRect(sx,y+72,selectionW,20),9,Cyan,false,true);
 }else{
  label("FIELD COMMAND",NSMakeRect(sx,y+17,selectionW,25),small?13:18,Ink,true);label("Select a unit or structure\nto issue orders.",NSMakeRect(sx,y+52,selectionW,52),11,Muted);
  if(!small)label("Destroy the enemy Anchor.\nGather ore. Expand. Outmaneuver.",NSMakeRect(sx,y+120,selectionW,52),11,Muted);
 }
 std::vector<UIButton> actions;
 auto add=[&](std::string name,std::string detail,int action,Kind kind=Kind::Worker,int parameter=0,bool enabled=true){actions.push_back({{},name,detail,action,kind,parameter,enabled});};
 if(e&&definition(e->kind).building){
  if(e->progress<1){if(!sim.constructionWorker(e->id))add("Resume build","Assign nearest Drudge",ResumeBuild);add("Cancel build","Refund unfinished",Cancel);}
  else{
   for(const auto&d:definitions())if(!d.building&&d.kind!=Kind::Resource&&d.producer==e->kind)add(d.name,std::to_string(d.cost)+" ore · T"+std::to_string(d.tier),Train,d.kind,0,sim.players()[0].ore>=d.cost&&sim.players()[0].tier>=d.tier);
   if(e->kind==Kind::Laboratory){add("Advance tier",std::to_string(500*sim.players()[0].tier)+" ore · unlock units",Research,Kind::Worker,0,sim.players()[0].tier<3&&sim.players()[0].ore>=500*sim.players()[0].tier);add("Weapon tech",std::to_string(200*(sim.players()[0].weapons+1))+" ore · + attack",Research,Kind::Worker,1,sim.players()[0].weapons<3&&sim.players()[0].ore>=200*(sim.players()[0].weapons+1));add("Armor tech",std::to_string(200*(sim.players()[0].armor+1))+" ore · + armor",Research,Kind::Worker,2,sim.players()[0].armor<3&&sim.players()[0].ore>=200*(sim.players()[0].armor+1));}
   add("Rally point","R · choose target",Rally);if(!e->queue.empty())add("Cancel queue","Refund next item",Cancel);
  }
 }else if(e){
  bool worker=false;for(Id id:selected){const auto*s=sim.find(id);if(s&&s->kind==Kind::Worker)worker=true;}
  if(worker){for(Kind kind:{Kind::Processor,Kind::Foundry,Kind::MotorPool,Kind::Laboratory,Kind::Turret,Kind::Headquarters}){const auto&d=definition(kind);add(d.name,std::to_string(d.cost)+" ore · T"+std::to_string(d.tier),Build,kind,0,sim.players()[0].ore>=d.cost&&sim.players()[0].tier>=d.tier);}}
  add("Attack move","A · choose target",AttackMode);add("Stop","X · clear orders",Stop);add("Hold position","V · guard ground",Hold);
 }
 if(actions.empty()){add("Select army","F2",Army);add("Anchor","B / Home",Home);add("Controls","H",Help);}
 int columns=actions.size()>6?3:3;double gap=6,bw=(actionW-gap*(columns-1))/columns,bh=small?30:48;int rows=(int)(actions.size()+columns-1)/columns;
 double ay=y+14;
 if(!small){label("COMMAND DECK",NSMakeRect(actionX,y+13,actionW,14),9,Muted,true,true);ay+=22;}
 for(size_t i=0;i<actions.size();i++){
  UIButton b=actions[i];b.rect=NSMakeRect(actionX+(i%columns)*(bw+gap),ay+(i/columns)*(bh+gap),bw,bh);if(small)b.detail.clear();[self drawButton:b context:c accent:(buildingMode&&b.action==Build&&b.kind==placement)||(attackMode&&b.action==AttackMode)];
 }
 if(!small&&rows<3)label("Cmd+1–9 assign groups · 1–9 recall / quick command",NSMakeRect(actionX,y+178,actionW,15),9,Muted,false,true);
 if(!menu){
  std::string objective="OBJECTIVE   ELIMINATE ENEMY ANCHOR";label(objective,NSMakeRect(20,world.origin.y+15,430,18),10,Muted,true,true);
  if(toastUntil>CACurrentMediaTime()){
   bool critical=criticalUntil>CACurrentMediaTime();double tw=std::min(W-40.,820.);box(c,NSMakeRect((W-tw)/2,NSMaxY(world)-43,tw,30),critical?Color{.20,.06,.055,.98}:Color{.025,.05,.07,.95});line(c,NSMakePoint((W-tw)/2,NSMaxY(world)-43),NSMakePoint((W-tw)/2+3,NSMaxY(world)-13),critical?Coral:Cyan,3);
   label(toast,NSMakeRect((W-tw)/2+13,NSMaxY(world)-36,tw-23,20),11,Ink);
  }
  if(attackMode||rallyMode||buildingMode)label(buildingMode?"PLACEMENT MODE":attackMode?"ATTACK MOVE":"RALLY POINT",NSMakeRect(W/2-130,world.origin.y+17,260,20),12,Amber,true,true,NSTextAlignmentCenter);
 }
}
- (void)drawMenu:(CGContextRef)c {
 double W=self.bounds.size.width,H=self.bounds.size.height;box(c,self.bounds,Color{.015,.027,.044,.68});
 double width=std::min(680.,W-64),left=std::max(32.,W*.075),top=std::max(38.,H*.16);bool compact=H<600;
 label("ORIGINAL REAL-TIME STRATEGY",NSMakeRect(left,top,width,20),10,Cyan,true,true);
 label("CINDERLINE",NSMakeRect(left-3,top+29,width,80),compact?49:72,Ink,true);
 line(c,NSMakePoint(left,top+114),NSMakePoint(left+57,top+114),Cyan,3);
 label("The last seam is worth fighting for.",NSMakeRect(left,top+134,width,30),compact?16:22,Ink);
 if(!compact)label("Build a foothold on the fracture worlds. Command a mechanized force,\nsecure the crystal seams, and break your rival’s command network.",NSMakeRect(left,top+177,width,54),14,Muted);
 double sy=top+(compact?180:256);label("CHOOSE BATTLEFIELD",NSMakeRect(left,sy,width,15),9,Muted,true,true);
 std::array<std::string,3> maps={"Shattered Rift","Glass Basin","Iron Reach"};double mw=std::min(172.,(W-left-40)/3-8);
 for(int i=0;i<3;i++)[self drawButton:{NSMakeRect(left+i*(mw+8),sy+24,mw,compact?33.:52.),maps[i],compact?"":(i==0?"BALANCED / TWO LANES":i==1?"OPEN / WIDE FLANKS":"TIGHT / CHOKE POINTS"),Map0+i} context:c accent:mapChoice==i];
 double by=sy+(compact?70:95);
 [self drawButton:{NSMakeRect(left,by,252,compact?43.:55.),"BEGIN SKIRMISH   →","",Begin} context:c accent:YES];
 [self drawButton:{NSMakeRect(left+265,by,146,compact?43.:55.),"FIELD MANUAL","",Help} context:c accent:NO];
 if(!compact)label("ONE COMMANDER  /  AI OPPONENT  /  THREE SECTORS",NSMakeRect(left,by+83,width,18),9,Muted,false,true);
 label("DEVELOPMENT RUNNER · SHARED C++ SIMULATION · UNREAL PROJECT INCLUDED",NSMakeRect(24,H-26,W-48,16),9,Muted,false,true);
 if(W>1150){
  double rx=W*.72,ry=H*.45,r=std::min(180.,H*.22);ring(c,NSMakeRect(rx-r,ry-r,r*2,r*2),Color{.22,.91,.84,.12});ring(c,NSMakeRect(rx-r*.8,ry-r*.8,r*1.6,r*1.6),Color{.22,.91,.84,.13});
  poly(c,{{rx,ry-r*.88},{rx+r*.57,ry-r*.28},{rx+r*.31,ry+r*.68},{rx,ry+r*.93},{rx-r*.31,ry+r*.68},{rx-r*.57,ry-r*.28}},Color{.10,.23,.25,.94},Color{.22,.91,.84,.6},2);
  poly(c,{{rx,ry-r*.62},{rx+r*.31,ry-r*.22},{rx,ry+r*.54},{rx-r*.31,ry-r*.22}},Color{.13,.37,.37,1},Cyan);
  line(c,NSMakePoint(rx-r*.22,ry-r*.12),NSMakePoint(rx+r*.22,ry-r*.12),Cyan,4);
  label("V E C T O R  C O M M A N D",NSMakeRect(rx-r-50,ry+r+31,r*2+100,24),10,Cyan,true,true,NSTextAlignmentCenter);
 }
}
- (void)drawOverlay:(CGContextRef)c {
 double W=self.bounds.size.width,H=self.bounds.size.height;box(c,self.bounds,Color{.009,.020,.034,.86});buttons.clear();
 bool result=sim.winner()>=0&&!menu&&!showHelp;double width=std::min(760.,W-48),height=std::min(H-40.,showHelp?520.:result?480.:355.);NSRect r=NSMakeRect((W-width)/2,(H-height)/2,width,height);
 box(c,r,Panel);borderRect(c,r,Edge);double x=r.origin.x+28,y=r.origin.y+23;
 label(showHelp?"FIELD MANUAL":result?(sim.winner()==0?"SECTOR SECURED":"COMMAND LOST"):"TACTICAL PAUSE",NSMakeRect(x,y,width-56,40),showHelp?25:32,result?(sim.winner()==0?Cyan:Coral):Ink,true);
 line(c,NSMakePoint(x,y+53),NSMakePoint(x+46,y+53),Cyan,3);
 if(showHelp){
  std::vector<std::pair<std::string,std::string>> help={{"SELECT","Click friendly units · drag a box · Shift adds · double click same type"},{"COMMAND","Click ground: move · enemy: attack · crystal: gather · A then click: attack-move"},{"CAMERA","Arrow keys / WASD · right or middle drag · Space drag · scroll to zoom"},{"ECONOMY","Drudges harvest ore. Siphons add supply. Kilns produce infantry."},{"TECHNOLOGY","Build a Resonator to advance tiers and improve weapons / armor."},{"SPECIALISTS","Needles pierce armor · Cinderthrows siege · Mends heal · Veils fly"},{"SHORTCUTS","F2 army · B base · X stop · V hold · R rally · Cmd+1–9 assigns groups"},{"SESSION","Esc/P pause · H manual · F5 save · F9 load · destroy enemy Anchor"}};
  double row=std::min(43.,(height-172)/help.size());for(size_t i=0;i<help.size();i++){double yy=y+73+i*row;label(help[i].first,NSMakeRect(x,yy,130,18),9,Cyan,true,true);label(help[i].second,NSMakeRect(x+133,yy,width-194,34),width<700?10:12,Ink);}
  [self drawButton:{NSMakeRect(x,NSMaxY(r)-64,180,38),"CLOSE MANUAL","",Resume,Kind::Worker,2} context:c accent:YES];
 }else if(result){
  label(sim.winner()==0?"The rival command network is silent. The seam is yours.":"Your Anchor has fallen. Regroup and return to the fracture.",NSMakeRect(x,y+78,width-56,40),13,Muted);
  const auto&s=sim.players()[0].stats;std::vector<std::pair<std::string,std::string>> stats={{"TIME",clockText(sim.time())},{"ORE GATHERED",std::to_string(s.gathered)},{"UNITS PRODUCED",std::to_string(s.produced)},{"ENEMIES ELIMINATED",std::to_string(s.killed)},{"UNITS LOST",std::to_string(s.lost)},{"STRUCTURES BUILT",std::to_string(s.built)}};
  double start=y+(height<430?111:140),cellW=(width-56)/3;for(size_t i=0;i<stats.size();i++){double xx=x+(i%3)*cellW,yy=start+(i/3)*(height<430?60:76);label(stats[i].first,NSMakeRect(xx,yy,cellW-12,15),9,Muted,true,true);label(stats[i].second,NSMakeRect(xx,yy+23,cellW-12,32),25,Ink,true,true);}
  [self drawButton:{NSMakeRect(x,NSMaxY(r)-74,210,46),"REMATCH","",Rematch} context:c accent:YES];[self drawButton:{NSMakeRect(x+225,NSMaxY(r)-74,170,46),"MAIN MENU","",QuitMenu} context:c accent:NO];
 }else{
  label("Simulation paused. Your orders will wait.",NSMakeRect(x,y+81,width-56,30),14,Muted);
  [self drawButton:{NSMakeRect(x,y+137,210,47),"RESUME OPERATION","",Resume} context:c accent:YES];[self drawButton:{NSMakeRect(x+225,y+137,175,47),"FIELD MANUAL","",Help} context:c accent:NO];
  [self drawButton:{NSMakeRect(x,y+200,125,43),"SAVE","F5",Save} context:c accent:NO];[self drawButton:{NSMakeRect(x+137,y+200,125,43),"LOAD","F9",Load} context:c accent:NO];[self drawButton:{NSMakeRect(x+274,y+200,126,43),"MAIN MENU","",QuitMenu} context:c accent:NO];
 }
}
- (NSArray*)accessibilityChildren {
 NSMutableArray* items=[NSMutableArray array];
 for(const auto&b:buttons){CinderlineAccessibilityButton* item=[[CinderlineAccessibilityButton alloc] init];item->owner=self;item->button=b;[item setAccessibilityElement:YES];[item setAccessibilityRole:NSAccessibilityButtonRole];[item setAccessibilityFrame:[[self window] convertRectToScreen:[self convertRect:b.rect toView:nil]]];[item setAccessibilityLabel:[NSString stringWithUTF8String:b.label.c_str()]];[item setAccessibilityParent:self];[item setAccessibilityEnabled:b.enabled];[items addObject:item];}return items;
}
- (NSArray*)accessibilityChildrenInNavigationOrder {return [self accessibilityChildren];}
- (NSArray*)accessibilityVisibleChildren {return [self accessibilityChildren];}
- (BOOL)runRenderStress:(int)frames {
 [timer invalidate];timer=nil;
 if(menu)[self startGame];
 // Exercise the same handlers/layout used by the live UI, without sending
 // system events or creating a window. Restore the loaded match afterward.
 Simulation savedSim=sim;auto savedSelection=selected;Camera savedCamera=camera;
 BOOL savedMenu=menu,savedPause=paused,savedHelp=showHelp;menu=paused=showHelp=NO;
 Id ember=sim.debugSpawn(Kind::Striker,0,{900,750}),needle=sim.debugSpawn(Kind::Lancer,0,{940,750}),outside=sim.debugSpawn(Kind::Striker,0,{980,750});
 for(NSSize size:{NSMakeSize(1440,900),NSMakeSize(844,390)}){
  [self setFrameSize:size];camera.zoom=size.height<560?.30:.68;[self home];selected={ember,needle};
  NSBitmapImageRep* target=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:size.width pixelsHigh:size.height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
  NSGraphicsContext* raw=[NSGraphicsContext graphicsContextWithBitmapImageRep:target];CGContextTranslateCTM(raw.CGContext,0,size.height);CGContextScaleCTM(raw.CGContext,1,-1);[NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:raw.CGContext flipped:YES]];[self drawRect:self.bounds];[NSGraphicsContext restoreGraphicsState];
  int chips=0;UIButton emberChip;
  for(const auto&b:buttons)if(b.action==FilterType){++chips;if(b.kind==Kind::Striker)emberChip=b;if(!NSContainsRect(self.bounds,b.rect)){fprintf(stderr,"Subgroup chip outside viewport\n");return NO;}for(const auto&other:buttons)if(other.action!=FilterType&&NSIntersectsRect(b.rect,other.rect)){fprintf(stderr,"Subgroup chip overlaps command button\n");return NO;}}
  if(chips!=2){fprintf(stderr,"Mixed selection lacks type chips\n");return NO;}auto hash=sim.stateHash();[self performAction:emberChip];
  if(selected.size()!=1||!selected.count(ember)||selected.count(outside)||sim.stateHash()!=hash){fprintf(stderr,"UI subgroup activation changed unrelated units/orders\n");return NO;}
  NSArray* arguments=NSProcessInfo.processInfo.arguments;NSUInteger captureIndex=[arguments indexOfObject:@"--regression-captures"];
  if(captureIndex!=NSNotFound&&captureIndex+1<arguments.count){NSString* filename=size.width<1000?@"native-subgroup-compact-qa.png":@"native-subgroup-desktop-qa.png";[[target representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[arguments[captureIndex+1] stringByAppendingPathComponent:filename] atomically:YES];}
 }
 for(NSEventModifierFlags modifiers:std::array<NSEventModifierFlags,4>{0,NSEventModifierFlagCommand,NSEventModifierFlagControl,NSEventModifierFlagOption}){
  attackMode=NO;NSEvent* downEvent=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:modifiers timestamp:CACurrentMediaTime() windowNumber:0 context:nil characters:@"a" charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
  NSEvent* upEvent=[NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:CACurrentMediaTime() windowNumber:0 context:nil characters:@"a" charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
  [self keyDown:downEvent];[self keyUp:upEvent];if((bool)attackMode!=(modifiers==0)){fprintf(stderr,"Modified A incorrectly armed attack-move\n");return NO;}
 }
 Config alertFixture;alertFixture.ai=false;sim.reset(alertFixture);observedBuildingHP.clear();criticalUntil=attackAlertCooldown=0;Id anchor=0;for(const auto&e:sim.entities())if(e.team==0&&e.kind==Kind::Headquarters){anchor=e.id;observedBuildingHP[e.id]=e.hp;}
 Id attacker=sim.debugSpawn(Kind::Lancer,1,{800,600});Command assault;assault.team=1;assault.type=CommandType::Attack;assault.units={attacker};assault.target=anchor;sim.command(assault);lastFrame=CACurrentMediaTime()-.1;[self tick:nil];
 if(criticalUntil<=CACurrentMediaTime()||toast.find("UNDER ATTACK")==std::string::npos){fprintf(stderr,"Structure damage did not raise priority warning\n");return NO;}std::string warning=toast;[self message:"Production completed"];if(toast!=warning){fprintf(stderr,"Production notification replaced critical warning\n");return NO;}
 sim=std::move(savedSim);selected=std::move(savedSelection);camera=savedCamera;menu=savedMenu;paused=savedPause;showHelp=savedHelp;attackMode=NO;observedBuildingHP.clear();criticalUntil=attackAlertCooldown=0;[self setFrameSize:NSMakeSize(1440,900)];
 fprintf(stdout,"PASS: mixed-selection chip activation/layout at desktop and compact sizes; modifier-A guards; damage warning survives production notification.\n");
 NSBitmapImageRep* bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:1440 pixelsHigh:900 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
 NSGraphicsContext* bitmapContext=[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
 CGContextTranslateCTM(bitmapContext.CGContext,0,900);CGContextScaleCTM(bitmapContext.CGContext,1,-1);
 NSGraphicsContext* context=[NSGraphicsContext graphicsContextWithCGContext:bitmapContext.CGContext flipped:YES];
 @try {
  for(int i=0;i<frames;i++){@autoreleasepool{
   // One offscreen frame for each simulation tick stresses queues and deaths.
   sim.update(Simulation::Step);
   if(i%80==0){selected.clear();const auto&all=sim.entities();if(!all.empty()){const auto&e=all[(i/80)%all.size()];if(e.team==0&&e.alive())selected.insert(e.id);}}
   if(i%500==0){showHelp=(i/500)%4==1;paused=(i/500)%4==2;menu=(i/500)%4==3;}
   [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:context];[self drawRect:self.bounds];[NSGraphicsContext restoreGraphicsState];
   if(i%1000==999){fprintf(stdout,"Rendered %d frames; simulation %.1fs\n",i+1,sim.time());fflush(stdout);}
  }}
 }@catch(NSException* exception){fprintf(stderr,"Render stress exception: %s — %s\n",exception.name.UTF8String,exception.reason.UTF8String);return NO;}
 fprintf(stdout,"PASS: %d offscreen frames, %.1f simulated seconds, no Cocoa drawing exception.\n",frames,sim.time());return YES;
}
- (void)drawRect:(NSRect)dirty {
 camera.clamp([self worldRect]);CGContextRef c=[[NSGraphicsContext currentContext] CGContext];CGContextSetShouldAntialias(c,true);buttons.clear();[self drawBattlefield:c rect:[self worldRect]];
 if(menu){[self drawMenu:c];}else [self drawHUD:c];
 if(showHelp||paused||(!menu&&sim.winner()>=0))[self drawOverlay:c];
}
@end

@interface CinderlineAppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@end
@implementation CinderlineAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
 NSRect screen=[[NSScreen mainScreen] visibleFrame];double width=std::min(1440.,screen.size.width-70),height=std::min(900.,screen.size.height-80);
 NSArray* args=[[NSProcessInfo processInfo] arguments];NSUInteger wi=[args indexOfObject:@"--width"],hi=[args indexOfObject:@"--height"];if(wi!=NSNotFound&&wi+1<args.count)width=[args[wi+1] doubleValue];if(hi!=NSNotFound&&hi+1<args.count)height=[args[hi+1] doubleValue];
 self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,width,height) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
 self.window.title=@"Cinderline // Tactical RTS";self.window.minSize=NSMakeSize(800,420);self.window.backgroundColor=[NSColor blackColor];self.window.contentView=[[CinderlineView alloc] initWithFrame:NSMakeRect(0,0,width,height)];
 [self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
 NSMenu* menuBar=[[NSMenu alloc] init];NSMenuItem* appMenuItem=[[NSMenuItem alloc] init];[menuBar addItem:appMenuItem];NSMenu* appMenu=[[NSMenu alloc] initWithTitle:@"Cinderline"];[appMenu addItemWithTitle:@"Quit Cinderline" action:@selector(terminate:) keyEquivalent:@"q"];[appMenuItem setSubmenu:appMenu];[NSApp setMainMenu:menuBar];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {return YES;}
@end

int main(int argc,const char* argv[]) {
 for(int i=1;i<argc;i++)if(std::string(argv[i])=="--render-stress"){@autoreleasepool{[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];CinderlineView* view=[[CinderlineView alloc] initWithFrame:NSMakeRect(0,0,1440,900)];return [view runRenderStress:i+1<argc?std::max(1,atoi(argv[i+1])):10000]?0:1;}}
 for(int i=1;i<argc;i++)if(std::string(argv[i])=="--smoke-test"){
  Camera c;c.x=1322;c.y=827;c.zoom=.64;for(NSRect r:{NSMakeRect(0,58,1440,638),NSMakeRect(0,58,844,200)})for(Vec2 p: {Vec2{0,0},Vec2{4700,4500},Vec2{1234,987}}){Vec2 q=c.world(c.screen(p,r),r);if(hypot(q.x-p.x,q.y-p.y)>.01){fprintf(stderr,"camera roundtrip failed\n");return 1;}}
  for(NSRect r:{NSMakeRect(0,58,1440,638),NSMakeRect(0,58,844,200)})for(Vec2 corner:{Vec2{0,0},Vec2{4800,4800}}){c.x=corner.x;c.y=corner.y;c.zoom=.05;c.clamp(r);Vec2 a=c.world(r.origin,r),b=c.world(NSMakePoint(NSMaxX(r),NSMaxY(r)),r);if(a.x<-.01||a.y<-.01||b.x>4800.01||b.y>4800.01){fprintf(stderr,"camera viewport escaped world bounds\n");return 1;}}
  for(NSRect area:{NSMakeRect(138,324,262,60),NSMakeRect(210,768,687,90)})for(int i=0;i<9;i++){NSRect a=subgroupRect(area,i);if(!NSContainsRect(area,a))return 1;for(int j=0;j<i;j++)if(NSIntersectsRect(a,subgroupRect(area,j)))return 1;}
  Simulation sim;if(sim.entities().empty()){fprintf(stderr,"shared simulation empty\n");return 1;}for(int i=0;i<20;i++)sim.update(Simulation::Step);if(sim.tick()<20)return 1;
  Id ember=sim.debugSpawn(Kind::Striker,0,{1200,1200}),needle=sim.debugSpawn(Kind::Lancer,0,{1300,1200}),otherEmber=sim.debugSpawn(Kind::Striker,0,{1400,1200});std::unordered_set<Id> selection{ember,needle};auto before=sim.stateHash();filterSelection(selection,sim,Kind::Striker);if(selection.size()!=1||!selection.count(ember)||selection.count(otherEmber)||sim.stateHash()!=before){fprintf(stderr,"subgroup filter changed unrelated units or orders\n");return 1;}
  printf("PASS: camera transforms/world bounds and subgroup layout at desktop/compact dimensions; subgroup filters only current selection without changing orders; shared simulation advanced %llu ticks.\n",(unsigned long long)sim.tick());return 0;
 }
 @autoreleasepool {NSApplication* app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];CinderlineAppDelegate* delegate=[[CinderlineAppDelegate alloc] init];[app setDelegate:delegate];[app run];}return 0;
}
