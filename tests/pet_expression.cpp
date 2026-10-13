#include "../main/pet_expression.hpp"
#include "../main/display_idle.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

static void bounded(const PetExpressionView &view){
 assert(std::isfinite(view.mouth_open));
 assert(view.mouth_open>=0.f&&view.mouth_open<=1.f);
}

int main(){
 PetMotionView motion;
 PetExpressionState voice;
 assert(voice.update(0,false,0.f,motion,0).expression==PetExpression::Happy);
 assert(!voice.view().animating);
 // Silence, including a held listening key, never opens the mouth.
 for(uint32_t now=20;now<500;now+=20){
  bounded(voice.update(now,true,0.005f,motion,0));
  assert(voice.view().mouth_open==0.f&&voice.view().expression==PetExpression::Happy);
 }
 for(uint32_t now=500;now<1000;now+=20)bounded(voice.update(now,true,0.022f,motion,0));
 assert(voice.view().expression==PetExpression::Talking);
 assert(voice.view().mouth_open>0.20f&&voice.view().mouth_open<0.40f);
 for(uint32_t now=1000;now<1500;now+=20)bounded(voice.update(now,true,0.060f,motion,0));
 assert(voice.view().expression==PetExpression::Laugh);
 const float loud=voice.view().mouth_open;
 voice.update(1500,false,0.060f,motion,0);
 assert(voice.view().mouth_open<loud&&voice.view().mouth_open>0.f);
 for(uint32_t now=1520;now<3500;now+=20)bounded(voice.update(now,false,0.060f,motion,0));
 assert(voice.view().expression==PetExpression::Happy&&voice.view().mouth_open==0.f&&!voice.view().animating);
 // One significant but not dizzy collision gets a brief surprised expression.
 PetExpressionState hit;hit.update(0,false,0.f,motion,0);
 motion.impact=0.5f;motion.dizzy=0.20f;
 assert(hit.update(25,false,0.f,motion,0).expression==PetExpression::Surprised);
 motion.impact=0.f;motion.dizzy=0.f;
 assert(hit.update(500,false,0.f,motion,0).expression==PetExpression::Happy);
 // Strong anger overrides dizziness and speaking. Recovery stays contextual:
 // only a previously angry pet looks briefly sad, then shy, then normal.
 PetExpressionState reaction;reaction.update(0,false,0.f,motion,0);
 motion.angry=0.9f;motion.dizzy=0.9f;
 assert(reaction.update(100,true,0.1f,motion,0).expression==PetExpression::Angry);
 motion.angry=0.f;
 assert(reaction.update(200,true,0.1f,motion,0).expression==PetExpression::Dizzy);
 // Clear the voice envelope before the physical recovery finishes.
 for(uint32_t now=220;now<2000;now+=20)reaction.update(now,false,0.f,motion,0);
 motion.dizzy=0.f;
 assert(reaction.update(2000,false,0.f,motion,0).expression==PetExpression::Sad);
 assert(reaction.update(2700,false,0.f,motion,700).expression==PetExpression::Shy);
 assert(reaction.update(3950,false,0.f,motion,1950).expression==PetExpression::Happy);
 // A dizzy-only recovery skips sadness. A fresh key press/speech takes
 // precedence immediately so cosmetic recovery never interferes with use.
 motion.dizzy=0.9f;reaction.update(4000,false,0.f,motion,0);motion.dizzy=0.f;
 assert(reaction.update(4100,false,0.f,motion,0).expression==PetExpression::Shy);
 assert(reaction.update(4125,true,0.f,motion,0).expression==PetExpression::Happy);
 // Every automatic expression is only visual; the screen still sleeps at its
 // 30-minute deadline and quiet Sleepy requires no continuous animation.
 PetExpressionState rest;DisplayIdle display(0);bool saw_wink=false,saw_sleepy=false;
 for(uint32_t now=0;now<=1800100;now+=20){
  const auto &view=rest.update(now,false,0.f,motion,now);bounded(view);
  saw_wink|=view.expression==PetExpression::Wink;saw_sleepy|=view.expression==PetExpression::Sleepy;
  if(view.expression==PetExpression::Sleepy)assert(!view.animating);
  display.update(now,false);
 }
 assert(saw_wink&&saw_sleepy&&!display.awake());
 assert(rest.update(1800120,true,0.f,motion,0).expression==PetExpression::Happy);
 // Invalid telemetry is harmless and does not synthesize expressions. A long
 // pause closes the mouth instead of prolonging the old audio animation.
 motion.angry=std::numeric_limits<float>::quiet_NaN();
 motion.dizzy=std::numeric_limits<float>::infinity();motion.impact=-1.f;
 assert(voice.update(60000,true,std::numeric_limits<float>::quiet_NaN(),motion,0).expression==PetExpression::Happy);
 assert(voice.view().mouth_open==0.f);
 // All state ages and recovery timers are the same through uint32 wrap.
 PetExpressionState wrapped,ordinary;
 for(uint32_t t=0;t<140000;t+=25){
  motion={};
  if(t>=1000 && t<2000){motion.angry=0.8f;motion.dizzy=0.8f;}
  const bool talk=t>=5000&&t<6000;
  const auto &a=wrapped.update(0xfffff000u+t,talk,0.026f,motion,t);
  const auto &b=ordinary.update(t,talk,0.026f,motion,t);
  assert(a.expression==b.expression&&a.animating==b.animating);
  assert(std::fabs(a.mouth_open-b.mouth_open)<0.00001f);
 }
 std::puts("pet expression: eight poses, voice mouth, reaction priority, recovery, sleep, noise, invalid data and wrap passed");
}
