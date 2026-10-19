#include "CompressorEditor.h"
#include "../Shared/PlotStroke.h"
#include "../Shared/FrozenInspection.h"

#include "CompressorTheme.h"
#include <cstdlib>


namespace {
class CompressorValueLabel final : public juce::Label {
public:
    explicit CompressorValueLabel(juce::Slider& owner):slider(owner) {}
    void mouseDown(const juce::MouseEvent& e) override { gesture.reset();armed=isEnabled()&&!isBeingEdited()&&e.mods.isLeftButtonDown();origin=e.position.y;start=slider.valueToProportionOfLength(slider.getValue());fine=e.mods.isShiftDown(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!armed)return;
        if(gesture&&fine!=e.mods.isShiftDown()){origin=e.position.y;start=slider.valueToProportionOfLength(slider.getValue());}
        fine=e.mods.isShiftDown();const auto distance=origin-e.position.y;
        if(!gesture&&std::abs(distance)<3)return;
        if(!gesture)gesture=std::make_unique<juce::Slider::ScopedDragNotification>(slider);
        slider.setValue(slider.proportionOfLengthToValue(juce::jlimit(0.0,1.0,start+distance*(fine?.1:1)/200)),juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent& e) override {const bool dragged=gesture!=nullptr;gesture.reset();armed=false;if(!dragged&&isEnabled()&&!e.mouseWasDraggedSinceMouseDown()&&getLocalBounds().contains(e.getPosition()))showEditor();}
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override {}
    void cancel(){armed=false;gesture.reset();hideEditor(true);}
private: juce::Slider& slider;std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;bool armed=false,fine=false;float origin=0;double start=0;
};
}

CompressorLookAndFeel::CompressorLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId,T::text);setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId,T::panel2.withAlpha(.88f));setColour(juce::ComboBox::textColourId,T::text);setColour(juce::ComboBox::outlineColourId,T::grid);
    setColour(juce::PopupMenu::backgroundColourId,T::panel);setColour(juce::PopupMenu::textColourId,T::text);setColour(juce::PopupMenu::highlightedBackgroundColourId,T::cyan.withAlpha(.35f));
}
void CompressorLookAndFeel::drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider&s)
{
    juce::Graphics::ScopedSaveState state(g);
    const auto colour=[&](juce::Colour c){g.setColour(c.withMultipliedAlpha(s.isEnabled()?1.0f:.28f));};
    const bool detail=(bool)s.getProperties()["detailKnob"];auto c=juce::Point<float>(x+w*.5f,y+h*(detail?.5f:.45f));float r=juce::jmin(w,h)*(detail?.32f:.34f),a=start+pos*(end-start);juce::Path base,on;
    const float surround=detail?6.f:8.f;colour(T::panel2.withAlpha(detail?.45f:.78f));g.fillEllipse(c.x-r-surround,c.y-r-surround,(r+surround)*2,(r+surround)*2);base.addCentredArc(c.x,c.y,r,r,0,start,end,true);const float origin=(s.getName()=="INPUT"||s.getName()=="OUTPUT")?(start+end)*.5f:start;on.addCentredArc(c.x,c.y,r,r,0,origin,a,true);
    colour(T::grid.withAlpha(detail?.72f:1.f));g.strokePath(base,juce::PathStrokeType(4.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));colour((s.getName()=="THRESHOLD"||s.getName()=="SC HP"?T::amber:s.getName()=="RMS TIME"?T::rms:T::lime).withAlpha(detail?.8f:1.f));g.strokePath(on,juce::PathStrokeType(4.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    colour(T::panel.withAlpha(detail?.58f:1.f));g.fillEllipse(c.x-r+6,c.y-r+6,(r-6)*2,(r-6)*2);auto tip=c+juce::Point<float>(std::sin(a),-std::cos(a))*r*.58f;colour(T::text.withAlpha(detail?.88f:1.f));g.drawLine({c,tip},2.f);
}
void CompressorLookAndFeel::drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float sliderPos,float,float,juce::Slider::SliderStyle,juce::Slider& slider)
{ if((bool)slider.getProperties()["valueOnly"])return;const float cy=y+h*.5f;g.setColour(T::panel2.withAlpha(.82f));g.fillRoundedRectangle((float)x,cy-3,(float)w,6,3);g.setColour(T::lime.withAlpha(.85f));g.fillRoundedRectangle((float)x,cy-2,juce::jmax(0.f,sliderPos-x),4,2);g.setColour(T::text);g.fillEllipse(sliderPos-4,cy-4,8,8); }
void CompressorLookAndFeel::drawToggleButton(juce::Graphics&g,juce::ToggleButton&b,bool over,bool down)
{
    juce::Graphics::ScopedSaveState state(g);g.beginTransparencyLayer(b.isEnabled()?1.f:.28f);
    const auto a=b.getLocalBounds().toFloat().reduced(1);const bool on=b.getToggleState(),top=(bool)b.getProperties()["topBar"];
    const bool external=b.getButtonText()=="SC"&&(bool)b.getProperties()["externalActive"];
    const bool audition=b.getButtonText()=="SC"&&(bool)b.getProperties()["auditionActive"];
    const auto accent=b.getButtonText()=="SHOW SC"||b.getButtonText()=="AUDITION SC"?T::violet:b.getComponentID()=="detectorView"?T::cyan.interpolatedWith(T::rms,(float)b.getProperties().getWithDefault("rmsBlend",(bool)b.getProperties()["rmsTrace"]?1.f:0.f)):T::lime;
    auto fill=on?(top?T::lime.withAlpha(.9f):accent.withAlpha(.22f)):audition?T::violet.withAlpha(.32f):external?T::lime.withAlpha(.32f):top?T::panel2.withAlpha(.88f):T::bg.withAlpha(.7f);
    if(b.getButtonText()=="BYPASS"&&on)fill=T::red;
    if(over)fill=fill.brighter(.08f);if(down)fill=fill.darker(.1f);
    g.setColour(fill);g.fillRoundedRectangle(a,5);
    if(!top){g.setColour(T::grid);g.drawRoundedRectangle(a,5,1);}
    if(audition){g.setColour(T::violet);g.fillRect(a.getX()+8,a.getBottom()-3,a.getWidth()-16,2.f);}
    g.setColour(on?top?T::bg:accent:audition?T::violet:external?T::lime:T::dim);
    g.setFont(juce::FontOptions(9.5f,juce::Font::bold));g.drawText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred);
    g.endTransparencyLayer();
}
void CompressorLookAndFeel::drawComboBox(juce::Graphics&g,int w,int h,bool,int,int,int,int,juce::ComboBox& box)
{ const float alpha=box.isEnabled()?1.f:.28f;g.setColour(T::panel2.withAlpha(.9f*alpha));g.fillRoundedRectangle(0,0,(float)w,(float)h,5);g.setColour(T::grid.withAlpha(alpha));g.drawRoundedRectangle(.5f,.5f,w-1.f,h-1.f,5,1);g.setColour(T::cyan.withAlpha(alpha));juce::Path p;p.startNewSubPath(w-24.f,h*.5f-2.5f);p.lineTo(w-18.f,h*.5f+2.5f);p.lineTo(w-12.f,h*.5f-2.5f);g.strokePath(p,juce::PathStrokeType(2.f)); }
juce::Font CompressorLookAndFeel::getComboBoxFont(juce::ComboBox&){return juce::Font(juce::FontOptions(10.5f,juce::Font::plain));}
juce::Label* CompressorLookAndFeel::createSliderTextBox(juce::Slider& slider)
{ auto* label=new CompressorValueLabel(slider);label->setEditable(true,false,false);label->setJustificationType(juce::Justification::centred);label->setColour(juce::Label::textColourId,T::dim);label->setColour(juce::Label::textWhenEditingColourId,T::dim);if((bool)slider.getProperties()["detailKnob"])label->setFont(juce::FontOptions(9.5f));return label; }
void CompressorLookAndFeel::drawLabel(juce::Graphics&g,juce::Label&l)
{
    valueStyle.paintLabel(g,l,T::bg,valueAccent(l));
    if(l.isBeingEdited())return;
    auto* slider=dynamic_cast<juce::Slider*>(l.getParentComponent());
    const auto live=slider?slider->getProperties()["autoReleaseReadout"].toString():juce::String();
    g.setColour(l.findColour(juce::Label::textColourId).withMultipliedAlpha(live.isNotEmpty()?.95f:l.isEnabled()?1.0f:.28f));
    g.setFont(live.isNotEmpty()?juce::Font(juce::FontOptions(10.5f)):l.getFont());
    g.drawFittedText(live.isNotEmpty()?live:l.getText(),l.getLocalBounds(),l.getJustificationType(),1,l.getMinimumHorizontalScale());
}
juce::Rectangle<int> CompressorLookAndFeel::getTooltipBounds(const juce::String&,juce::Point<int> point,juce::Rectangle<int> area)
{return juce::Rectangle<int>(point.x+12,point.y+20,310,58).constrainedWithin(area);}
void CompressorLookAndFeel::drawTooltip(juce::Graphics&g,const juce::String& text,int w,int h)
{g.setColour(T::panel2);g.fillRoundedRectangle(0,0,(float)w,(float)h,5);g.setColour(T::text);g.setFont(juce::FontOptions(11));g.drawFittedText(text,10,6,w-20,h-12,juce::Justification::centredLeft,3);}

double CompressorHistoryClock::advance (uint64_t sequence, uint64_t epoch, double now, double rate)
{
    const double latest = (double) sequence;
    const double elapsed = lastTime < 0 ? 0 : juce::jmax (0.0, now - lastTime);
    if (position < 0 || epoch != lastEpoch || sequence < lastSequence || sequence == 0)
        position = latest;
    else
        // The viewport may coast at most one frame beyond publication, but
        // never waits behind it. Only scrolling is interpolated: capture reads
        // the newest *committed* frame, never future/unpublished audio.
        position = juce::jlimit (latest, latest + 1.0, position + elapsed * rate / 1000);
    lastTime = now; lastSequence = sequence; lastEpoch = epoch;
    return position;
}


CompressorDisplay::CompressorDisplay(OpenCompressorAudioProcessor&x):p(x)
{
    for(auto* fade:{&sidechainFade,&detectorFade,&focusFade,&kneeFade})fade->setBypassed(true,0,false);
    threshold=p.parameters.getParameter("threshold");
    thresholdDisplayAttachment=std::make_unique<juce::ParameterAttachment>(*threshold,[this](float value)
    {
        displayedThreshold=value;
        repaint();
    });
    thresholdDisplayAttachment->sendInitialUpdate();
    const std::array<const char*,4> ids{"ratio","knee","range","sidechainHistoryView"};
    for(size_t i=0;i<ids.size();++i){sceneAttachments[i]=std::make_unique<juce::ParameterAttachment>(*p.parameters.getParameter(ids[i]),[this,sc=i==3](float value){if(sc)setSidechainVisible(value>.5f);cachedSequence=UINT64_MAX;repaint();});sceneAttachments[i]->sendInitialUpdate();}
    bypassDisplayAttachment=std::make_unique<juce::ParameterAttachment>(*p.parameters.getParameter("bypass"),[this](float value)
    {
        bypassFade.setBypassed(value>.5f,juce::Time::getMillisecondCounterHiRes(),displayActive);
        repaint();
    });
    bypassDisplayAttachment->sendInitialUpdate();
    setComponentID("historyDisplay");
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setDisplayActive(true);
}
CompressorDisplay::~CompressorDisplay() { setDisplayActive(false); if (dragging) threshold->endChangeGesture(); }
void CompressorDisplay::setHistoryAppearance(BypassFade& fade,bool& state,bool enabled)
{
    if(state==enabled)return;state=enabled;
    fade.setBypassed(!enabled,juce::Time::getMillisecondCounterHiRes(),displayActive);repaint();
}
void CompressorDisplay::setSidechainVisible(bool enabled){setHistoryAppearance(sidechainFade,sidechainVisible,enabled);}
void CompressorDisplay::setDetectorVisible(bool enabled){setHistoryAppearance(detectorFade,detectorVisible,enabled);}
void CompressorDisplay::setSidechainFocus(bool enabled){setHistoryAppearance(focusFade,sidechainFocus,enabled);}
void CompressorDisplay::setKneeVisible(bool enabled){setHistoryAppearance(kneeFade,showKnee,enabled);}
void CompressorDisplay::visibilityChanged()
{
    setDisplayActive(isVisible());
}
void CompressorDisplay::setDisplayActive(bool enabled)
{
    if(displayActive==enabled)return;
    displayActive=enabled;bypassFade.settle();badgeMotion.settle();
    for(auto* fade:{&sidechainFade,&detectorFade,&focusFade,&kneeFade})fade->settle();
    stopTimer();vblank={};
    frozen=inspecting=false;inspected=-1;p.editorState.frozen=false;
    if(!enabled){
        p.stopHistoryCapture();
        if(dragging){dragging=false;threshold->endChangeGesture();if(onReferenceEditEnd)onReferenceEditEnd();}
        return;
    }
    captureState=p.startHistoryCapture();captureStartSequence=capturedSequence=p.getHistorySequence();capturedEpoch=p.getHistoryEpoch();
    frames={};validFrames=0;scrollFraction=rasterPhase=0;clock.reset();waveImage={};cachedSequence=cachedEpoch=UINT64_MAX;
    lastVblankArrival=lastVblankTimestamp=-1;framePacer.reset();
    vblank=juce::VBlankAttachment(this,[this](double timestamp){vblankCallback(timestamp);});startTimerHz(60);repaint();
}
void CompressorLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down) {
 auto area=b.getLocalBounds().toFloat().reduced(1);auto colour=b.getToggleState()?T::lime.withAlpha(.25f):T::panel2.withAlpha(.7f);if(over)colour=colour.brighter(.08f);if(down)colour=colour.darker(.1f);g.setColour(colour.withMultipliedAlpha(b.isEnabled()?1.f:.3f));g.fillRoundedRectangle(area,5);if(!(bool)b.getProperties()["topBar"]){g.setColour(T::grid);g.drawRoundedRectangle(area,5,1);}
}
void CompressorLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {g.setColour(T::dim.withMultipliedAlpha(b.isEnabled()?1.f:.3f));g.setFont(juce::FontOptions(9.2f,juce::Font::bold));g.drawText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred);}
void OpenCompressorAudioProcessorEditor::Panel::paint(juce::Graphics& g){g.setColour(T::panel.withAlpha(.82f));g.fillRoundedRectangle(getLocalBounds().toFloat(),7);g.setColour(T::grid);g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),7,1);}
void CompressorDisplay::setThreshold(float y){float db=juce::jmap(juce::jlimit(0.f,(float)getHeight(),y),(float)getHeight(),0.f,-36.f,0.f);threshold->setValueNotifyingHost(threshold->convertTo0to1(db));}

juce::Rectangle<float> CompressorDisplay::thresholdBadgeBounds() const {
    const float ty=-displayedThreshold*getHeight()/36.f;
    // The old GR/MAX card no longer reserves the top of the plot.
    const auto clampedY=[&](int top){return juce::jlimit(6.f,(float)juce::jmax(6,top-26),ty-25.f);};
    const float y=badgeGeometryReady?badgeMotion.position(clampedY(badgeMainTop),clampedY(badgePageTop)):clampedY(controlsTop);
    return {22,y,146,22};
}
bool CompressorDisplay::hitThreshold(juce::Point<float> point) const {return std::abs(point.y+displayedThreshold*getHeight()/36.f)<9||thresholdBadgeBounds().contains(point);}
void CompressorDisplay::mouseDown(const juce::MouseEvent& e) {
    if(!hitThreshold(e.position)){
        if(frozen&&e.position.y>38&&e.position.y<controlsTop&&validFrames>0){inspecting=true;inspected=juce::jlimit((int)frames.size()-validFrames,(int)frames.size()-1,(int)(e.position.x/getWidth()*frames.size()));repaint();}
        return;
    }
    if(onReferenceEdit)onReferenceEdit();p.parameters.copyState();p.undoManager.beginNewTransaction("Threshold");
    dragging=true;dragStartY=e.position.y;dragStartDb=displayedThreshold;fineDrag=e.mods.isShiftDown();threshold->beginChangeGesture();repaint();
}
void CompressorDisplay::mouseDrag(const juce::MouseEvent& e) {
    if(inspecting){inspected=juce::jlimit((int)frames.size()-validFrames,(int)frames.size()-1,(int)(e.position.x/getWidth()*frames.size()));repaint();return;}
    if(!dragging)return;
    if(fineDrag!=e.mods.isShiftDown()){dragStartY=e.position.y;dragStartDb=displayedThreshold;fineDrag=e.mods.isShiftDown();}
    const float db=juce::jlimit(-36.f,0.f,dragStartDb+(dragStartY-e.position.y)*36.f/getHeight()*(fineDrag?.1f:1.f));
    threshold->setValueNotifyingHost(threshold->convertTo0to1(db));
}
void CompressorDisplay::mouseUp(const juce::MouseEvent&) {
    inspecting=false;if(!dragging)return;threshold->endChangeGesture();dragging=false;p.parameters.copyState();p.undoManager.beginNewTransaction();if(onReferenceEditEnd)onReferenceEditEnd();repaint();
}
void CompressorDisplay::mouseDoubleClick(const juce::MouseEvent& e) {
    if(!hitThreshold(e.position))return;
    if(dragging){threshold->endChangeGesture();dragging=false;}
    if(onReferenceEdit)onReferenceEdit();p.parameters.copyState();p.undoManager.beginNewTransaction("Reset threshold");
    threshold->beginChangeGesture();threshold->setValueNotifyingHost(threshold->getDefaultValue());threshold->endChangeGesture();p.parameters.copyState();p.undoManager.beginNewTransaction();if(onReferenceEditEnd)onReferenceEditEnd();
}
void CompressorDisplay::mouseMove(const juce::MouseEvent& e){setMouseCursor(hitThreshold(e.position)?juce::MouseCursor::UpDownResizeCursor:frozen?juce::MouseCursor::LeftRightResizeCursor:juce::MouseCursor::NormalCursor);repaint();}
void CompressorDisplay::mouseExit(const juce::MouseEvent&){setMouseCursor(juce::MouseCursor::NormalCursor);}
void CompressorDisplay::setFrozen(bool enabled) {
    if(enabled&&!frozen)captureHistory(true);
    frozen=enabled;p.editorState.frozen=enabled;inspected=-1;
    if(frozen)p.rememberFrozenHistory(frames,validFrames);else{clock.reset();captureHistory();}
    repaint();
}
void CompressorDisplay::restoreView(){cachedSequence=UINT64_MAX;frozen=p.editorState.frozen.load();if(frozen&&!p.restoreFrozenHistory(frames,validFrames))captureHistory(true);else if(!frozen){clock.reset();captureHistory();}repaint();}
void CompressorDisplay::captureHistory(bool latest,double now) {
 if(!displayActive)return;
 const auto sequence=p.getHistorySequence(),epoch=p.getHistoryEpoch();
 const double position=latest?(double)sequence:clock.advance(sequence,epoch,now,p.historyFramesPerSecond());
 const auto target=sequence;
 if(epoch!=capturedEpoch||target<captureStartSequence)captureStartSequence=0;
 if(target==capturedSequence&&epoch==capturedEpoch){scrollFraction=(float)(position-target);return;}
 const bool incremental=epoch==capturedEpoch&&target>capturedSequence&&target-capturedSequence<frames.size();
 const int count=(int)juce::jmin(incremental?target-capturedSequence:target-captureStartSequence,(uint64_t)frames.size());
 auto next=incremental?frames:decltype(frames){};
 if(incremental)std::move(next.begin()+count,next.end(),next.begin());
 for(int i=0;i<count;++i)if(!p.readHistoryFrame(target-(uint64_t)count+1+(uint64_t)i,next[next.size()-(size_t)count+(size_t)i],captureState))return;
 if(epoch!=p.getHistoryEpoch())return;
 frames=next;validFrames=incremental?juce::jmin((int)frames.size(),validFrames+count):count;capturedSequence=target;capturedEpoch=epoch;scrollFraction=(float)(position-target);
}
void CompressorDisplay::advanceHistory(double now)
{
    if(!displayActive)return;
    const auto old=capturedSequence,epoch=capturedEpoch;const auto fraction=scrollFraction;
    if(!frozen)captureHistory(false,now);
    const std::array<float,4> values{p.reductionLevel(0),p.reductionLevel(1),p.reductionPeakMarker(0),p.reductionPeakMarker(1)};
    if(old!=capturedSequence||epoch!=capturedEpoch||!juce::approximatelyEqual(fraction,scrollFraction)||values!=previousMeterValues){previousMeterValues=values;repaint();}
}
void CompressorDisplay::timerCallback(){if(displayActive&&isShowing())fallbackFrame(juce::Time::getMillisecondCounterHiRes());}
void CompressorDisplay::fallbackFrame(double timeMs)
{
    if(lastVblankArrival<0||timeMs-lastVblankArrival>100)animationFrame(timeMs);
}
void CompressorDisplay::vblankCallback(double timestampSeconds)
{
    if(displayActive&&isShowing())presentationFrame(timestampSeconds,juce::Time::getMillisecondCounterHiRes());
}
void CompressorDisplay::presentationFrame(double timestampSeconds,double arrival)
{
    const double nativeMs=timestampSeconds*1000;
    // Native/JUCE clocks have different epochs. Rebase on resets or long gaps,
    // exactly as Gate/Clipper do, rather than issuing catch-up repaint bursts.
    if(lastVblankTimestamp<0||nativeMs<lastVblankTimestamp||arrival-lastVblankArrival>250){vblankOffset=arrival-nativeMs;framePacer.reset();}
    lastVblankTimestamp=nativeMs;lastVblankArrival=arrival;
    if(framePacer.present(nativeMs))animationFrame(nativeMs+vblankOffset);
}
void CompressorDisplay::animationFrame(double timeMs)
{
    if(!displayActive)return;
    const bool bypassChanged=bypassFade.advance(timeMs);
    // Page requests start on the UI clock. Native presentation timestamps may
    // predict ahead, so keep badge travel on elapsed UI time at a real peer.
    const double badgeTime=getPeer()!=nullptr?juce::Time::getMillisecondCounterHiRes():timeMs;
    bool appearanceChanged=false;for(auto* fade:{&sidechainFade,&detectorFade,&focusFade,&kneeFade})appearanceChanged=fade->advance(badgeTime)||appearanceChanged;
    if(badgeMotion.advance(badgeTime)||bypassChanged||appearanceChanged)repaint();
    advanceHistory(timeMs);
    if(onAnimationFrame)onAnimationFrame(timeMs);
}
void CompressorDisplay::paint(juce::Graphics& g) {
    if(capturedSequence==0&&!frozen&&p.getHistorySequence()>0)captureHistory(true);
    const auto a=getLocalBounds().toFloat();g.fillAll(T::bg);
    auto dbY=[&](float db){return -db*a.getHeight()/36.f;};
    for(float db:{0.f,-3.f,-6.f,-9.f,-12.f,-15.f,-18.f,-21.f,-24.f}){const bool minor=(int)std::abs(db)%6!=0;g.setColour(T::grid.withAlpha(minor?.18f:.38f));g.drawHorizontalLine(juce::roundToInt(dbY(db)),22,a.getRight());}
    const float appearance=bypassFade.amount();
    const float scOpacity=sidechainFade.amount(),traceOpacity=detectorFade.amount(),focus=focusFade.amount();
    const bool showSC=scOpacity>0;
    const std::array<float,3> historyAppearance{scOpacity,traceOpacity,focus};
    const float dx=a.getWidth()/(frames.size()-1);
    constexpr float reductionStroke=1.5f; // Same weight and rounded joins as Limiter's GR contour.
    const float scale=juce::jmax(2.f,g.getInternalContext().getPhysicalPixelScaleFactor());
    // Rasterise in physical pixels on a fixed 1/64-pixel grid, anchored to
    // absolute frame identities. Recomputing local float coordinates/stroke
    // normals otherwise changes old peaks' coverage when copied by one pixel.
    const double pixelOrigin=std::floor((double)capturedSequence*dx*scale);
    // Unrecorded audio stays blank; only GR has a full-width neutral baseline.
    auto edge=[&](auto value,bool reduction=false){juce::Path path;const int measured=(int)frames.size()-validFrames,first=reduction?0:measured;for(int i=first;i<(int)frames.size();++i){const float db=reduction?(i<measured?0.f:-value(frames[(size_t)i])):juce::Decibels::gainToDecibels(value(frames[(size_t)i]),-120.f);const float y=(float)(std::round(juce::jlimit(-12.f,a.getHeight()+12,reduction?juce::jmax(reductionStroke*.5f,dbY(db)):dbY(db))*scale*64.0)/64.0);const float x=(float)(std::round(((double)capturedSequence+i)*dx*scale*64.0)/64.0-pixelOrigin);if(i==first)path.startNewSubPath(x,y);else path.lineTo(x,y);}if(!path.isEmpty())path.lineTo((a.getWidth()+dx+3+rasterPhase)*scale,path.getCurrentPosition().y);return path;};
    const int imageWidth=juce::roundToInt((a.getWidth()+dx+4)*scale),imageHeight=juce::roundToInt(a.getHeight()*scale);
    if(waveImage.isNull()||waveImage.getWidth()!=imageWidth||waveImage.getHeight()!=imageHeight||!juce::approximatelyEqual(cachedScale,scale)||cachedSequence!=capturedSequence||cachedEpoch!=capturedEpoch||cachedSC!=showSC||cachedHistoryAppearance!=historyAppearance){
        const bool sameSize=waveImage.getWidth()==imageWidth&&waveImage.getHeight()==imageHeight&&juce::approximatelyEqual(cachedScale,scale);
        const uint64_t advanced=capturedSequence>cachedSequence?capturedSequence-cachedSequence:0;
        const bool incremental=sameSize&&cachedEpoch==capturedEpoch&&cachedSC==showSC&&cachedHistoryAppearance==historyAppearance&&advanced>0&&advanced<=16;
        juce::Rectangle<int> changed(0,0,imageWidth,imageHeight);
        juce::RectangleList<int> changedRegions;
        if(!sameSize)waveImage=juce::Image(juce::Image::ARGB,imageWidth,imageHeight,true,juce::SoftwareImageType{});
        else {
            // A native drawing backend may retain the old image until present.
            waveImage.duplicateIfShared();
            if(incremental){
                // Absolute raster phase makes the scroll an exact integer-pixel
                // copy, retaining the original antialias coverage of old peaks.
                const int shift=juce::roundToInt(std::floor((double)capturedSequence*dx*scale)-std::floor((double)cachedSequence*dx*scale));
                waveImage.moveImageSection(0,0,shift,0,imageWidth-shift,imageHeight);
                const float phase=(float)(std::fmod((double)capturedSequence*dx*scale,1.0)/scale);
                const int left=juce::jlimit(0,imageWidth,(int)std::floor((a.getWidth()-advanced*dx+phase-4.f)*scale));
                changed={left,0,imageWidth-left,imageHeight};
                // Cropping the old first segment is not equivalent to the
                // new path's first endpoint/cap. Refresh that small edge too.
                const juce::Rectangle<int> leftEdge(0,0,juce::roundToInt(4*scale),imageHeight);
                waveImage.clear(leftEdge);changedRegions.add(leftEdge);
                // The GR outline can graze the top clip. Its endpoint coverage
                // depends on clipping, so refresh this thin boundary as well.
                const juce::Rectangle<int> topEdge(0,0,imageWidth,juce::roundToInt(4*scale));
                waveImage.clear(topEdge);changedRegions.add(topEdge);
            }
            waveImage.clear(changed);
        }
        changedRegions.add(changed);
        rasterPhase=(float)(std::fmod((double)capturedSequence*dx*scale,1.0)/scale);
        juce::Graphics wave(waveImage);wave.reduceClipRegion(changedRegions);wave.reduceClipRegion({0,0,imageWidth,juce::roundToInt((getHeight()-1)*scale)});
        // Close only the measured span. Closing a partial history to x=0
        // invents a diagonal ramp when inserted on an already-playing track.
        auto shade=[&](const juce::Path& path,juce::Colour colour,float alpha,bool fromTop=false){if(path.isEmpty())return;auto fill=path;fill.lineTo(path.getCurrentPosition().x,fromTop?0:a.getBottom()*scale);fill.lineTo(path.getBounds().getX(),fromTop?0:a.getBottom()*scale);fill.closeSubPath();wave.setGradientFill({colour.withAlpha(alpha),0,0,colour.withAlpha(alpha*.25f),0,a.getBottom()*scale,false});wave.fillPath(fill);};
        const auto in=edge([](const auto& f){return f.input;}),out=edge([](const auto& f){return f.output;}),gr=edge([](const auto& f){return f.reduction;},true);
        shade(in,T::cyan,juce::jmap(focus,.14f,.07f));shade(out,T::lime,juce::jmap(focus,.22f,.09f));wave.setColour(T::lime.withAlpha(juce::jmap(focus,.45f,.22f)));wave.strokePath(out,juce::PathStrokeType(.85f*scale));
        shade(gr,T::amber.darker(.24f),.22f,true);wave.setColour(T::amber.withAlpha(.72f));wave.strokePath(gr,juce::PathStrokeType(reductionStroke*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        if(traceOpacity>0&&validFrames>0){
            // Colour belongs to the recorded frame, never the current mode.
            // Split at midpoints between unlike frames, retaining a continuous
            // contour without connecting unrelated colour fills across runs.
            const int first=(int)frames.size()-validFrames,last=(int)frames.size()-1;
            auto point=[&](int i){const float db=juce::Decibels::gainToDecibels(frames[(size_t)i].detector,-120.f);return juce::Point<float>{(float)(std::round(((double)capturedSequence+i)*dx*scale*64.0)/64.0-pixelOrigin),(float)(std::round(juce::jlimit(-12.f,a.getHeight()+12,dbY(db))*scale*64.0)/64.0)};};
            for(int start=first;start<=last;){
                const bool rms=frames[(size_t)start].detectorRms;int end=start;
                while(end<last&&frames[(size_t)(end+1)].detectorRms==rms)++end;
                juce::Path detector;detector.startNewSubPath(start==first?point(start):(point(start-1)+point(start))*.5f);
                for(int i=start;i<=end;++i)detector.lineTo(point(i));
                if(end<last)detector.lineTo((point(end)+point(end+1))*.5f);
                else detector.lineTo((a.getWidth()+dx+3+rasterPhase)*scale,point(end).y);
                const auto colour=rms?T::rms:T::cyan;shade(detector,colour,.08f*traceOpacity);wave.setColour(colour.withAlpha(.78f*traceOpacity));wave.strokePath(detector,juce::PathStrokeType(.75f*scale));
                start=end+1;
            }
        }
        if(showSC){const auto sc=edge([](const auto& f){return f.sidechain;});shade(sc,T::violet,juce::jmap(focus,.20f,.27f)*scOpacity);wave.setColour(T::violet.withAlpha(juce::jmap(focus,.8f,1.f)*scOpacity));wave.strokePath(sc,juce::PathStrokeType(.75f*scale));}
        cachedSequence=capturedSequence;cachedEpoch=capturedEpoch;cachedSC=showSC;cachedScale=scale;cachedHistoryAppearance=historyAppearance;
    }
    g.setOpacity(1.f);g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageTransformed(waveImage,juce::AffineTransform::scale(1.f/scale).translated(-rasterPhase-scrollFraction*dx,0));
    g.setGradientFill({T::bg.withAlpha(0.f),0,a.getHeight()*.48f,T::bg.withAlpha(.7f),0,a.getBottom(),false});g.fillRect(a.withTrimmedTop(a.getHeight()*.48f));
    for(float db:{0.f,-3.f,-6.f,-9.f,-12.f,-15.f,-18.f,-21.f,-24.f}){const bool minor=(int)std::abs(db)%6!=0;g.setColour(T::dim.withAlpha(minor?.58f:1.f));g.setFont(juce::FontOptions(minor?7.5f:8.f));g.drawText(juce::String((int)std::abs(db)),2,juce::jlimit(0,getHeight()-12,juce::roundToInt(dbY(db))-6),18,12,juce::Justification::centredLeft);}
    const float t=displayedThreshold,thresholdStroke=dragging?2.5f:1.4f;
    const float ty=plotStrokeCentre(dbY(t),a.getHeight(),thresholdStroke);g.setColour(T::amber.withAlpha(.3f+.7f*appearance));g.drawLine(0,ty,a.getRight(),ty,thresholdStroke);
    if(kneeFade.amount()*appearance>.001f){const float r=p.parameters.getRawParameterValue("ratio")->load(),k=p.parameters.getRawParameterValue("knee")->load(),range=p.parameters.getRawParameterValue("range")->load();juce::Path curve;for(int x=0;x<getWidth();++x){const float inDb=juce::jmap((float)x,0.f,(float)getWidth(),-36.f,0.f),y=dbY(inDb-OpenCompressorAudioProcessor::gainReductionDb(inDb,t,r,k,range));if(x==0)curve.startNewSubPath(0,y);else curve.lineTo((float)x,y);}g.setColour(T::text.withAlpha(.74f*appearance*kneeFade.amount()));g.strokePath(curve,juce::PathStrokeType(1.2f));}
    const auto badge=thresholdBadgeBounds();g.setColour(T::panel2.withAlpha(.92f));g.fillRoundedRectangle(badge,4);g.setColour(T::amber.withAlpha(.4f+.6f*appearance));g.setFont(juce::FontOptions(9.5f,juce::Font::bold));g.drawText("THRESHOLD  "+juce::String(std::abs(t)<.05f?0.f:t,1)+" dB",badge,juce::Justification::centred);

    if(frozen&&inspected>=0&&!dragging)
    {
        const auto& f=frames[(size_t)inspected];
        FrozenInspection::paint(g, getLocalBounds().toFloat(), inspected*dx, (float)controlsTop,
            ((double)frames.size()-1-inspected)/p.historyFramesPerSecond(),
            { { "IN", FrozenInspection::level(f.input, -120.f) },
              { "OUT", FrozenInspection::level(f.output, -120.f) },
              { "GR", juce::String(f.reduction, 1)+" dB" } },
            { { "SC", FrozenInspection::level(f.sidechain, -120.f) },
              { "DET", juce::String(juce::Decibels::gainToDecibels(f.detectorMinimum, -120.f), 1)
                +" "+juce::String::charToString(0x2026)+" "+FrozenInspection::level(f.detector, -120.f),
                "-120.0 … -120.0 dB" } }, T::panel2, T::text.interpolatedWith(juce::Colours::white,.5f), T::dim);
    }
}

void CompressorMeters::refresh()
{
    const auto now=juce::Time::getMillisecondCounterHiRes();
    const std::array<float,5> values{p.reductionLevel(0),p.reductionLevel(1),p.reductionPeakMarker(0),p.reductionPeakMarker(1),p.maximumReductionLevel()};
    bypassFade.setBypassed(p.parameters.getRawParameterValue("bypass")->load()>.5f,now);
    const bool changed=maximum.update(values[4],now),a=maximum.advance(now),b=bypassFade.advance(now);
    if(values!=previous||changed||a||b){previous=values;repaint();}
}

void CompressorMeters::paint(juce::Graphics&g)
{
    const float h=juce::jmax(1.f,getHeight()-14.f);
    g.fillAll(T::bg);
    const float appearance=.4f+.6f*bypassFade.amount();
    const auto readoutColour=T::dim;
    const auto text=[](float value){return value<.005f?juce::String("0.00"):"-"+juce::String(value,2);};
    // The stereo bars span x=4..30; keep both crossfade layers on their centre.
    g.setFont(juce::FontOptions(10.5f,juce::Font::bold));
    g.setColour(readoutColour.withAlpha(appearance*(1-maximum.opacity())));g.drawFittedText(text(maximum.oldValue()),0,0,34,14,juce::Justification::centred,1,.8f);
    g.setColour(readoutColour.withAlpha(appearance*maximum.opacity()));g.drawFittedText(text(maximum.value()),0,0,34,14,juce::Justification::centred,1,.8f);
    g.setColour(T::panel);g.fillRect(0.f,14.f,(float)getWidth(),h);
    g.addTransform(juce::AffineTransform::translation(0,14));
    const auto grY=[h](float db){return juce::jmap(juce::jlimit(0.f,36.f,db),0.f,36.f,0.f,h);};
    for(int channel=0;channel<2;++channel)
    {
        const float x=channel==0?4.f:19.f;
        const auto colour=channel==0?T::amber:T::amber.brighter(.22f);
        const auto track=juce::Rectangle<float>(x,0,11,h);
        g.setColour(T::bg);g.fillRoundedRectangle(track,2);
        g.setGradientFill({colour,track.getTopLeft(),colour.darker(.35f),track.getBottomLeft(),false});
        g.fillRoundedRectangle(track.withBottom(grY(p.reductionLevel(channel))),2);
        g.setColour(colour);
        g.fillRect(x-1,grY(p.reductionPeakMarker(channel))-1,13.f,2.f);
    }
    for(float db:{0.f,6.f,12.f,18.f,24.f,30.f}){int y=juce::roundToInt(grY(db));g.setColour(T::grid);g.drawHorizontalLine(y,32,37);g.setColour(T::dim);g.setFont(juce::FontOptions(8.f));g.drawText(juce::String((int)db),40,juce::jlimit(0,(int)h-12,y-6),24,12,juce::Justification::centredLeft);}
    for(float db:{3.f,9.f,15.f,21.f,27.f,33.f}){int y=juce::roundToInt(grY(db));g.setColour(T::grid.withAlpha(.42f));g.drawHorizontalLine(y,32,37);g.setColour(T::dim.withAlpha(.58f));g.setFont(juce::FontOptions(7.5f));g.drawText(juce::String((int)db),40,juce::jlimit(0,(int)h-12,y-6),24,12,juce::Justification::centredLeft);}
}

OpenCompressorAudioProcessorEditor::OpenCompressorAudioProcessorEditor(OpenCompressorAudioProcessor&x):AudioProcessorEditor(&x),p(x),display(x),meters(x),sidechainPreview(x)
{
    setLookAndFeel(&lnf);setSize(p.editorState.width.load(),p.editorState.height.load());setResizable(true,true);setResizeLimits(720,440,1200,760);addAndMakeVisible(display);addAndMakeVisible(meters);advancedPanel.setComponentID("advancedPanel");addChildComponent(advancedPanel);advancedPanel.setInterceptsMouseClicks(false,false);
    knob(ratio,ratioL,"RATIO"," : 1",4);
    knob(thresholdKnob,thresholdL,"THRESHOLD"," dB",0);
    display.addAndMakeVisible(thresholdKnob);
    knob(attack,attackL,"ATTACK"," ms",1);knob(release,releaseL,"RELEASE"," ms",30);knob(mix,mixL,"MIX","%",100);knob(output,outputL,"OUTPUT"," dB",0);
    knob(input,inputL,"INPUT"," dB",0);knob(knee,kneeL,"KNEE"," dB",6);knob(hold,holdL,"HOLD"," ms",0);knob(range,rangeL,"RANGE"," dB",60);knob(lookahead,lookaheadL,"LOOKAHEAD"," ms",0);knob(link,linkL,"LINK","%",100);for(auto*s:{&input,&knee,&hold,&range,&lookahead,&link}){s->getProperties().set("detailKnob",true);s->setTextBoxStyle(juce::Slider::TextBoxBelow,false,66,14);}for(auto*l:{&inputL,&kneeL,&holdL,&rangeL,&lookaheadL,&linkL})l->setFont(juce::FontOptions(7.2f,juce::Font::bold));
    knob(sideHighPass,sideHighPassL,"SC HP"," Hz",20);knob(sideLowPass,sideLowPassL,"SC LP"," Hz",20000);for(auto*s:{&sideHighPass,&sideLowPass}){s->getProperties().set("detailKnob",true);s->setTextBoxStyle(juce::Slider::TextBoxBelow,false,78,14);}for(auto*l:{&sideHighPassL,&sideLowPassL})l->setFont(juce::FontOptions(7.2f,juce::Font::bold));
    combo(detectorMode,{"Peak","RMS"});detectorMode.setComponentID("detectorModeControl");
    knob(rmsTime,rmsTimeL,"RMS TIME"," ms",20);rmsTime.setComponentID("rmsTimeControl");rmsTimeL.setComponentID("rmsTimeLabel");
    rmsTime.getProperties().set("detailKnob",true);rmsTime.setTextBoxStyle(juce::Slider::TextBoxBelow,false,54,14);rmsTimeL.setFont(juce::FontOptions(7.2f,juce::Font::bold));
    addChildComponent(detectorModeL);detectorModeL.setComponentID("detectorModeLabel");detectorModeL.setText("DETECTION",juce::dontSendNotification);detectorModeL.setJustificationType(juce::Justification::centred);detectorModeL.setColour(juce::Label::textColourId,T::dim);detectorModeL.setFont(juce::FontOptions(7.2f,juce::Font::bold));detectorModeL.setTooltip(detectorMode.getTooltip());
    detectorMode.setTooltip("Detection: Peak responds to instantaneous amplitude; RMS averages filtered key power. Existing projects default to Peak. Switching fades the detector over 10 ms; no added latency. Past TRACE keeps its recorded colour: cyan Peak, rose RMS.");
    detectorModeL.setTooltip(detectorMode.getTooltip());
    rmsTime.setTooltip("RMS power-averaging response, 5–100 ms; default 20 ms. Drag the value, Shift-drag for fine control, click to type. This is a time constant, not lookahead. Attack still controls gain response. No hidden +3 dB calibration.");rmsTimeL.setTooltip(rmsTime.getTooltip());
    detectorMode.onChange=[this]{updateDetectorControls();};
    combo(maxLookahead,{"Max 0 ms","Max 1 ms","Max 2 ms","Max 5 ms","Max 10 ms","Max 20 ms"});maxLookahead.setComponentID("maxLookaheadControl");button(autoGain,"AUTO GAIN");autoGain.setComponentID("autoGainControl");button(autoRelease,"AUTO RELEASE");button(smooth,"SMOOTH");smooth.setComponentID("smoothControl");button(detailsView,"DETAILS");detailsView.setComponentID("detailsView");button(sidechainView,"SC");sidechainView.setComponentID("sidechainView");button(externalSidechain,"EXT SC");externalSidechain.setComponentID("externalSidechainControl");button(sidechainHistoryView,"SHOW SC");sidechainHistoryView.setComponentID("sidechainHistoryViewControl");autoRelease.setComponentID("autoRelease");button(kneeView,"KNEE");button(bypass,"BYPASS");bypass.setComponentID("bypassControl");kneeView.onClick=[this]{p.editorState.knee=kneeView.getToggleState();display.setKneeVisible(detailsOpen&&kneeView.getToggleState());p.editorStateChanged();};detailsView.onStateChange=[this]{detailsOpen=detailsView.getToggleState();if(detailsOpen){sidechainOpen=false;sidechainView.setToggleState(false,juce::dontSendNotification);}p.editorState.page=detailsOpen?1:0;p.editorStateChanged();updateOverlayVisibility();resized();repaint();};sidechainView.onStateChange=[this]{sidechainOpen=sidechainView.getToggleState();if(sidechainOpen){detailsOpen=false;detailsView.setToggleState(false,juce::dontSendNotification);}p.editorState.page=sidechainOpen?2:0;p.editorStateChanged();updateOverlayVisibility();resized();repaint();};autoRelease.onStateChange=[this]{const bool manual=!autoRelease.getToggleState();release.setEnabled(manual);releaseL.setEnabled(manual);};
    externalSidechain.setTooltip("Use the host's mono or stereo Sidechain input as the detector. If no sidechain bus is routed, the internal signal remains active.");
    sidechainHistoryView.setTooltip("Show the selected key after HP/LP filtering. Ext SC and Audition SC turn this on once; you may hide it again. Mutually exclusive with Trace. Display only.");
    sidechainView.setTooltip("Show sidechain source and detector filters. Details and SC are mutually exclusive.");
    sideHighPass.setTooltip("24 dB/oct detector high-pass. 20 Hz–20 kHz scale; 20 Hz is Off. Stops at least 1/3 octave below LP without moving it. Drag the orange spectrum marker or type Hz/kHz.");
    sideLowPass.setTooltip("24 dB/oct detector low-pass. 20 Hz–20 kHz scale; 20 kHz is Off. Stops at least 1/3 octave above HP without moving it. Drag the mint spectrum marker or type Hz/kHz.");
    maxLookahead.setTooltip("Available when Smooth is on. Sets maximum lookahead and host-reported latency.");
    lookahead.setTooltip("Available when Smooth is on and Max exceeds 0 ms. Sets detector lead within Max.");
    smooth.setTooltip("Enables 1 ms gain smoothing and unlocks Max Lookahead and Lookahead.");
    autoGain.setTooltip("Measures dry and compressed wet levels; applies up to +18 dB to the wet signal before Mix.");
    autoRelease.setTooltip("Adapts release from reduction, signal history and crest factor; manual Release is disabled while active. Not FabFilter's manual-scaled Auto Release.");
    kneeView.setTooltip("Show or hide the compression transfer curve.");
    input.setTooltip("Input trim before compression. Double-click for 0 dB; click the value to type.");
    output.setTooltip("Output trim after Mix. Double-click for 0 dB.");
    thresholdKnob.setTooltip("Compression threshold. Drag its line or badge; Shift-drag for fine adjustment.");
    ratio.setTooltip("Compression ratio. 1:1 is neutral; double-click restores 4:1.");
    attack.setTooltip("Gain-envelope attack with finite minimum-attack transient overshoot. Old saved sessions retain their original detector/gain law. Type ms or seconds, for example 0.01 s.");
    release.setTooltip("Gain-recovery timing after Hold, with a quick first stage and a slower tail. Auto Release controls timing while active. Old saved sessions retain detector-domain release.");
    hold.setTooltip("Holds gain reduction before recovery; renewed compression restarts Hold. Old saved sessions retain detector hold.");
    knee.setTooltip("Width of the soft transition. It narrows near a 0 dB threshold to preserve unity.");
    range.setTooltip("Maximum gain reduction. 0 dB disables compression; double-click restores 60 dB.");
    mix.setTooltip("0% latency-aligned dry signal; 100% compressed signal. Auto Gain affects the wet path.");
    link.setTooltip("0% independent stereo detectors; 100% both channels follow the louder detector.");
    button(listen,"AUDITION SC");listen.setComponentID("sidechainListenControl");
    listen.setTooltip("Audition the filtered key and turn on Show SC. Normal I/O history dims while the SC contour comes forward. Escape, Bypass or hiding/closing the editor stops audition. Continues across page changes.");
    listen.onStateChange=[this]{p.setAuditionSidechain(listen.getToggleState()&&!bypass.getToggleState());display.setSidechainFocus(p.isAuditioningSidechain());syncView();};
    addAndMakeVisible(sidechainPreview);
    auto&s=p.parameters;auto attachS=[&](const char*id,juce::Slider&v){sliderAttachments.push_back(std::make_unique<SA>(s,id,v));};attachS("input",input);attachS("threshold",thresholdKnob);attachS("ratio",ratio);attachS("knee",knee);attachS("attack",attack);attachS("hold",hold);attachS("release",release);attachS("range",range);attachS("mix",mix);attachS("output",output);attachS("link",link);attachS("sideHighPass",sideHighPass);attachS("sideLowPass",sideLowPass);attachS("rmsTime",rmsTime);
    for(auto* slider:{&input,&thresholdKnob,&ratio,&knee,&attack,&hold,&release,&range,&lookahead,&mix,&output,&link,&sideHighPass,&sideLowPass,&rmsTime})
    {
        slider->setColour(juce::Slider::textBoxTextColourId,T::dim);
        slider->setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
        slider->setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        slider->textFromValueFunction=[slider](double value){const bool detail=(bool)slider->getProperties()["detailKnob"];if(detail&&slider->getTextValueSuffix().trim()=="Hz"&&value>=1000)return juce::String(value/1000.,3).trimCharactersAtEnd("0").trimCharactersAtEnd(".")+"k";const int places=slider->getNumDecimalPlacesToDisplay();if(std::abs(value)<.5*std::pow(10.0,-places))value=0.0;auto formatted=juce::String(value,places);return detail&&places>0?formatted.trimCharactersAtEnd("0").trimCharactersAtEnd("."):formatted;};
        slider->valueFromTextFunction=[slider](const juce::String& text)
        {
            const auto cleaned=text.trim().toLowerCase().replaceCharacter(',','.').replace(juce::String::charToString(0x2212),"-");
            const auto utf8=cleaned.toRawUTF8();char* end=nullptr;double value=std::strtod(utf8,&end);
            if(end==utf8||!std::isfinite(value))return slider->getValue();
            const auto unit=juce::String(end).trim();const auto suffix=slider->getTextValueSuffix().trim().toLowerCase();
            if(suffix=="hz"&&(unit=="k"||unit=="khz"||unit=="k hz"))value*=1000;
            else if(suffix=="ms"&&(unit=="s"||unit=="sec"))value*=1000;
            else if(unit.isNotEmpty()&&unit!=suffix&&!(suffix.contains(":")&&unit.removeCharacters(" ")==":1"))return slider->getValue();
            return juce::jlimit(slider->getMinimum(),slider->getMaximum(),value);
        };
        slider->updateText();
    }
    auto attachB=[&](const char*id,juce::ToggleButton&v){buttonAttachments.push_back(std::make_unique<BA>(s,id,v));};attachB("autoGain",autoGain);attachB("autoRelease",autoRelease);attachB("smooth",smooth);attachB("bypass",bypass);attachB("externalSidechain",externalSidechain);attachB("sidechainHistoryView",sidechainHistoryView);comboAttachments.push_back(std::make_unique<CA>(s,"maxLookahead",maxLookahead));
    comboAttachments.push_back(std::make_unique<CA>(s,"detectorMode",detectorMode));
    lookaheadAttachment=std::make_unique<juce::ParameterAttachment>(*s.getParameter("lookahead"),[this](float){updateLookaheadAvailability();});
    lookahead.onDragStart=[this]{lookaheadDragging=true;lookaheadAttachment->beginGesture();};
    lookahead.onDragEnd=[this]{lookaheadAttachment->endGesture();lookaheadDragging=false;};
    lookahead.onValueChange=[this]{if(!updatingLookahead){if(lookaheadDragging)lookaheadAttachment->setValueAsPartOfGesture((float)lookahead.getValue());else lookaheadAttachment->setValueAsCompleteGesture((float)lookahead.getValue());}};
    maxLookahead.onChange=[this]{updateLookaheadAvailability();};smooth.onStateChange=[this]{updateLookaheadAvailability();};externalSidechain.onStateChange=[this]{sidechainView.getProperties().set("externalActive",externalSidechain.getToggleState());sidechainView.repaint();};updateLookaheadAvailability();externalSidechain.onStateChange();autoRelease.onStateChange();updateOverlayVisibility();

    button(freeze,"FREEZE");freeze.setComponentID("historyFreeze");button(hoverHelp,"?");hoverHelp.setComponentID("hoverHelp");button(trace,"TRACE");trace.setComponentID("detectorView");
    freeze.setTooltip("Freeze the eight-second history and spectrum. Click or drag history to inspect input/output, GR, filtered SC and detector min/max.");
    hoverHelp.setTooltip("Enable hover explanations. Off by default; saved per instance.");
    trace.setTooltip("Actual linked, lookahead-aware detector: cyan for Peak, rose for RMS. Mutually exclusive with Show SC. Frozen inspection includes minimum/maximum. Display only.");
    freeze.onClick=[this]{display.setFrozen(freeze.getToggleState());p.editorStateChanged();};
    hoverHelp.onClick=[this]{p.editorState.help=hoverHelp.getToggleState();syncView();p.editorStateChanged();};
    trace.onClick=[this]{p.setDetectorTraceVisible(trace.getToggleState());display.setDetectorVisible(trace.getToggleState());p.editorStateChanged();};
    sidechainHistoryView.onStateChange=[this]{if(sidechainHistoryView.getToggleState()){trace.setToggleState(false,juce::dontSendNotification);display.setDetectorVisible(false);}};
    for(auto* b:{(juce::Button*)&undo,(juce::Button*)&redo,(juce::Button*)&snapshotA,(juce::Button*)&snapshotB,(juce::Button*)&copy})addAndMakeVisible(b);
    undo.setComponentID("undo");redo.setComponentID("redo");snapshotA.setComponentID("snapshotA");snapshotB.setComponentID("snapshotB");copy.setComponentID("copySnapshot");
    undo.onClick=[this]{stopAudition();p.parameters.copyState();p.undoManager.undo();timerCallback();};
    redo.onClick=[this]{stopAudition();p.parameters.copyState();p.undoManager.redo();timerCallback();};
    snapshotA.onClick=[this]{p.selectSnapshot(0);timerCallback();};snapshotB.onClick=[this]{p.selectSnapshot(1);timerCallback();};
    copy.onClick=[this]{p.copySnapshot();copyFeedback=20;timerCallback();};
    undo.setTooltip("Undo the last parameter edit. Command-Z.");redo.setTooltip("Redo the last parameter edit. Command-Shift-Z.");
    snapshotA.setTooltip("Compare sound A. Bypass, Smooth/Max latency and display settings stay unchanged.");snapshotB.setTooltip("Compare sound B.");copy.setTooltip("Copy current sound settings to the other comparison slot.");
    for(auto* b:{(juce::Button*)&undo,(juce::Button*)&redo,(juce::Button*)&hoverHelp,(juce::Button*)&freeze,(juce::Button*)&detailsView,(juce::Button*)&sidechainView,(juce::Button*)&bypass}){b->getProperties().set("topBar",true);b->setWantsKeyboardFocus(false);}
    addChildComponent(scStatus);scStatus.setComponentID("sidechainStatus");scStatus.setFont(juce::FontOptions(8.f));scStatus.setColour(juce::Label::textColourId,T::dim);
    sidechainPreview.setComponentID("sidechainResponsePreview");
    display.onReferenceEdit=[this]{referenceEditing=true;suspendPages();};
    display.onReferenceEditEnd=[this]{referenceEditing=false;restoreAfter=0;restorePages();};
    display.onAnimationFrame=[this](double timeMs){meters.refresh();advancePageFade(timeMs);advanceDetectorAppearance(timeMs);};
    addMouseListener(this,true);
    for(auto* slider:{&input,&thresholdKnob,&ratio,&knee,&attack,&hold,&release,&range,&lookahead,&mix,&output,&link,&sideHighPass,&sideLowPass,&rmsTime})
        for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))label->setTooltip(slider->getTooltip());
    for(auto pair:std::initializer_list<std::pair<juce::Slider*,juce::Label*>>{{&input,&inputL},{&thresholdKnob,&thresholdL},{&ratio,&ratioL},{&knee,&kneeL},{&attack,&attackL},{&hold,&holdL},{&release,&releaseL},{&range,&rangeL},{&lookahead,&lookaheadL},{&mix,&mixL},{&output,&outputL},{&link,&linkL},{&sideHighPass,&sideHighPassL},{&sideLowPass,&sideLowPassL}})pair.second->setTooltip(pair.first->getTooltip());
    setWantsKeyboardFocus(true);viewRevision=std::numeric_limits<uint64_t>::max();syncView();viewInitialized=true;resized();startTimerHz(15);timerCallback();
}
OpenCompressorAudioProcessorEditor::~OpenCompressorAudioProcessorEditor(){setDisplayActive(false);stopTimer();viewInitialized=false;display.onAnimationFrame={};removeMouseListener(this);stopAudition();sidechainPreview.endDrag();for(auto* slider:{&input,&thresholdKnob,&ratio,&knee,&attack,&hold,&release,&range,&lookahead,&mix,&output,&link,&sideHighPass,&sideLowPass,&rmsTime})for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<CompressorValueLabel*>(child))label->cancel();if(lookaheadDragging){lookaheadAttachment->endGesture();lookaheadDragging=false;}setLookAndFeel(nullptr);}
void OpenCompressorAudioProcessorEditor::stopAudition(){listen.setToggleState(false,juce::dontSendNotification);p.setAuditionSidechain(false);display.setSidechainFocus(false);}
bool OpenCompressorAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{if(key==juce::KeyPress::escapeKey){sidechainPreview.endDrag();stopAudition();return true;}
 if(dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()))return false;
 if(key.getModifiers().isCommandDown()&&juce::CharacterFunctions::toLowerCase(key.getTextCharacter())=='z'){stopAudition();p.parameters.copyState();if(key.getModifiers().isShiftDown())p.undoManager.redo();else p.undoManager.undo();timerCallback();return true;}return false;}
void OpenCompressorAudioProcessorEditor::setDisplayActive(bool enabled)
{
    if(displayActive==enabled)return;
    displayActive=enabled;display.setDisplayActive(enabled);meters.settleAppearance();sidechainPreview.setDisplayActive(enabled);updateDetectorControls();
    freeze.setToggleState(false,juce::dontSendNotification);
    if(!enabled){
        sidechainPreview.endDrag();stopAudition();
        for(auto* slider:{&input,&thresholdKnob,&ratio,&knee,&attack,&hold,&release,&range,&lookahead,&mix,&output,&link,&sideHighPass,&sideLowPass,&rmsTime}){
            slider->hideTextBox(true);for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<CompressorValueLabel*>(child))label->cancel();
            const auto position=slider->getLocalBounds().getCentre().toFloat();const auto now=juce::Time::getCurrentTime();
            slider->mouseUp({juce::Desktop::getInstance().getMainMouseSource(),position,{},1,0,0,0,0,slider,slider,now,position,now,1,false});
        }
        if(lookaheadDragging){lookaheadAttachment->endGesture();lookaheadDragging=false;}
        referenceEditing=false;restoreAfter=0;pagesSuspended=false;updateOverlayVisibility();
    }
    startTimerHz(enabled?15:2);
}
void OpenCompressorAudioProcessorEditor::visibilityChanged(){if(viewInitialized)setDisplayActive(isVisible());}
void OpenCompressorAudioProcessorEditor::parentHierarchyChanged(){if(viewInitialized&&getPeer()!=nullptr)setDisplayActive(isShowing());}
void OpenCompressorAudioProcessorEditor::focusLost(FocusChangeType){sidechainPreview.endDrag();}
void OpenCompressorAudioProcessorEditor::beginTransaction(){p.parameters.copyState();p.undoManager.beginNewTransaction();}
void OpenCompressorAudioProcessorEditor::mouseDown(const juce::MouseEvent& e){beginTransaction();if(e.eventComponent==&thresholdKnob||thresholdKnob.isParentOf(e.eventComponent)){referenceEditing=true;suspendPages();}}
void OpenCompressorAudioProcessorEditor::mouseUp(const juce::MouseEvent& e){p.parameters.copyState();p.undoManager.beginNewTransaction();if(e.eventComponent==&thresholdKnob||thresholdKnob.isParentOf(e.eventComponent)){referenceEditing=false;restoreAfter=juce::Time::getMillisecondCounterHiRes()+100;}}
void OpenCompressorAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails&){beginTransaction();if(e.eventComponent==&thresholdKnob||thresholdKnob.isParentOf(e.eventComponent)){restoreAfter=juce::Time::getMillisecondCounterHiRes()+350;suspendPages();}}
void OpenCompressorAudioProcessorEditor::suspendPages(){if(!pagesSuspended){pagesSuspended=true;updateOverlayVisibility();}}
void OpenCompressorAudioProcessorEditor::restorePages(){if(!pagesSuspended||referenceEditing||juce::Time::getMillisecondCounterHiRes()<restoreAfter)return;for(auto* child:thresholdKnob.getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))if(label->isBeingEdited())return;pagesSuspended=false;updateOverlayVisibility();}
void OpenCompressorAudioProcessorEditor::syncView(){
    const bool changed=!viewInitialized||viewRevision!=p.editorState.revision.load();
    if(changed||!viewInitialized){viewRevision=p.editorState.revision.load();detailsOpen=p.editorState.page==1;sidechainOpen=p.editorState.page==2;detailsView.setToggleState(detailsOpen,juce::dontSendNotification);sidechainView.setToggleState(sidechainOpen,juce::dontSendNotification);kneeView.setToggleState(p.editorState.knee.load(),juce::dontSendNotification);display.restoreView();setSize(p.editorState.width.load(),p.editorState.height.load());updateOverlayVisibility();resized();}
    hoverHelp.setToggleState(p.editorState.help.load(),juce::dontSendNotification);trace.setToggleState(p.editorState.trace.load(),juce::dontSendNotification);freeze.setToggleState(p.editorState.frozen.load(),juce::dontSendNotification);display.setDetectorVisible(trace.getToggleState());
    if(hoverHelp.getToggleState()&&!tooltips)tooltips=std::make_unique<juce::TooltipWindow>(this,700);else if(!hoverHelp.getToggleState())tooltips.reset();
}
void OpenCompressorAudioProcessorEditor::timerCallback()
{
    if(getPeer()!=nullptr)setDisplayActive(isShowing());
    if(!displayActive)return;
    syncView();restorePages();advancePageFade(juce::Time::getMillisecondCounterHiRes());updateLookaheadAvailability();updateDetectorControls();p.parameters.copyState();undo.setEnabled(p.undoManager.canUndo());redo.setEnabled(p.undoManager.canRedo());snapshotA.setToggleState(p.getSnapshot()==0,juce::dontSendNotification);snapshotB.setToggleState(p.getSnapshot()==1,juce::dontSendNotification);copy.setButtonText(copyFeedback-- >0?"COPIED":p.getSnapshot()==0?"COPY A>B":"COPY B>A");copyFeedback=juce::jmax(0,copyFeedback);
    const bool automatic=p.parameters.getRawParameterValue("autoRelease")->load()>.5f;
    release.getProperties().set("autoReleaseReadout",automatic?juce::String("Auto ")+juce::String::charToString(0x00b7)+" "+juce::String(juce::roundToInt(p.effectiveReleaseTime()))+" ms":juce::String());
    release.repaint();
    autoGain.setTooltip("Measured Auto Gain: +"+juce::String(p.automaticMakeupLevel(),1)+" dB compensation. Slow dry/wet power matching, not settings-based makeup. Applied only to the wet path before Mix.");
    autoRelease.setTooltip(p.usesLegacyDynamics()?"Legacy Auto Release: adapts from current reduction only; manual Release is disabled.":"Auto Release adapts from reduction, recent compression and crest factor; manual Release is disabled. Not FabFilter's manual-scaled Auto Release.");
    if(bypass.getToggleState()||p.isNonRealtime()||(listen.getToggleState()&&!p.isAuditioningSidechain()))stopAudition();
    sidechainView.getProperties().set("auditionActive",p.isAuditioningSidechain());sidechainView.repaint();
    display.setSidechainFocus(p.isAuditioningSidechain());
    const bool external=externalSidechain.getToggleState(),available=p.externalBusAvailable();
    scStatus.setText(external?available?"External sidechain\nHost bus enabled":"Ext. SC unavailable\nUsing internal input":"Internal detector\nInput + SC filters",juce::dontSendNotification);
    scStatus.setColour(juce::Label::textColourId,external&&available?T::violet:T::dim);
    if(sidechainPreview.isVisible()){sidechainPreview.updateSpectrum(display.isFrozen());sidechainPreview.repaint();}
    else p.setSpectrumEnabled(false);
    listen.setEnabled(!bypass.getToggleState());
}
void OpenCompressorAudioProcessorEditor::knob(juce::Slider&s,juce::Label&l,const juce::String&name,const juce::String&suffix,double def){s.setName(name);s.setComponentID(name.toLowerCase()+"Control");s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,78,18);s.setTextValueSuffix(suffix);s.setDoubleClickReturnValue(true,def);s.setMouseDragSensitivity(180);addAndMakeVisible(s);l.setText(name,juce::dontSendNotification);l.setComponentID(name.toLowerCase()+"Label");l.setJustificationType(juce::Justification::centred);l.setColour(juce::Label::textColourId,T::dim);l.setFont(juce::FontOptions(9,juce::Font::bold));addAndMakeVisible(l);}
void OpenCompressorAudioProcessorEditor::fader(juce::Slider&s,juce::Label&l,const juce::String&name,const juce::String&suffix,double def){s.setName(name);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,64,18);s.setTextValueSuffix(suffix);s.setDoubleClickReturnValue(true,def);addAndMakeVisible(s);l.setText(name,juce::dontSendNotification);l.setJustificationType(juce::Justification::centredLeft);l.setColour(juce::Label::textColourId,T::dim);l.setFont(juce::FontOptions(8.5f,juce::Font::bold));addAndMakeVisible(l);}
void OpenCompressorAudioProcessorEditor::button(juce::ToggleButton&b,const juce::String&t){b.setButtonText(t);b.setClickingTogglesState(true);addAndMakeVisible(b);}
void OpenCompressorAudioProcessorEditor::combo(juce::ComboBox&c,const juce::StringArray&items){c.addItemList(items,1);c.setJustificationType(juce::Justification::centred);addAndMakeVisible(c);}
void OpenCompressorAudioProcessorEditor::placeKnob(juce::Rectangle<int>a,juce::Slider&s,juce::Label&l){l.setBounds(a.removeFromTop(15));s.setBounds(a);}
void OpenCompressorAudioProcessorEditor::placeFader(juce::Rectangle<int>a,juce::Slider&s,juce::Label&l){l.setBounds(a.removeFromTop(13));s.setBounds(a);}
void OpenCompressorAudioProcessorEditor::updateOverlayVisibility()
{
    const int previous=pageFade.presented();
    if(previous==2&&(pagesSuspended||!sidechainOpen))sidechainPreview.endDrag();
    display.setBadgePageVisible(!pagesSuspended&&(detailsOpen||sidechainOpen),viewInitialized&&displayActive);
    pageFade.request(pagesSuspended?0:sidechainOpen?2:detailsOpen?1:0,
                     juce::Time::getMillisecondCounterHiRes(),viewInitialized&&displayActive);
    applyPageFade();
    if(previous!=pageFade.presented())resized();
}
void OpenCompressorAudioProcessorEditor::applyPageFade()
{
    const int page=pageFade.presented();const float alpha=pageFade.alpha();
    const bool interactive=pageFade.interactive()&&!pagesSuspended;
    advancedPanel.setVisible(page!=0);advancedPanel.setAlpha(alpha);
    juce::Component* details[]{&snapshotA,&snapshotB,&copy,&kneeView,&input,&inputL,&knee,&kneeL,&hold,&holdL,&range,&rangeL,&lookahead,&lookaheadL,&link,&linkL,&maxLookahead,&autoGain,&autoRelease,&smooth};
    for(auto*c:details){c->setVisible(page==1);c->setAlpha(alpha);c->setInterceptsMouseClicks(interactive,interactive);}
    juce::Component* sidechain[]{&trace,&scStatus,&sideHighPass,&sideHighPassL,&sideLowPass,&sideLowPassL,&externalSidechain,&sidechainHistoryView,&listen,&sidechainPreview,&detectorMode,&detectorModeL};
    for(auto*c:sidechain){c->setVisible(page==2);c->setAlpha(alpha);c->setInterceptsMouseClicks(interactive,interactive);}
    display.setKneeVisible(page==1&&detailsOpen&&!pagesSuspended&&kneeView.getToggleState());
    updateDetectorControls();
    display.setControlsTop((page!=0?advancedPanel.getY():thresholdL.getY())-display.getY());
}
void OpenCompressorAudioProcessorEditor::advancePageFade(double timeMs)
{
    if(!displayActive)return;
    const int previous=pageFade.presented();
    if(!pageFade.advance(timeMs))return;
    applyPageFade();
    if(previous!=pageFade.presented())resized();
}
void OpenCompressorAudioProcessorEditor::updateDetectorControls()
{
    const bool rms=p.parameters.getRawParameterValue("detectorMode")->load()>.5f;
    const double now=juce::Time::getMillisecondCounterHiRes();const bool animate=viewInitialized&&displayActive&&pageFade.presented()==2;
    if(!rms&&(bool)trace.getProperties()["rmsTrace"]){
        // A host may change mode while the user still holds RMS Time. End its
        // gesture before fading it away, rather than leaving a hidden edit live.
        const auto point=rmsTime.getLocalBounds().getCentre().toFloat();const auto eventTime=juce::Time::getCurrentTime();
        rmsTime.mouseUp({juce::Desktop::getInstance().getMainMouseSource(),point,{},1,0,0,0,0,&rmsTime,&rmsTime,eventTime,point,eventTime,1,false});
    }
    rmsTintFade.setBypassed(!rms,now,animate);
    if(!animate){rmsLayoutFade.setBypassed(!rms,now,false);rmsKnobFade.setBypassed(!rms,now,false);}
    else{
        // Reserve the full column before revealing the knob. On the way back,
        // fade the knob away first so the spectrum never moves underneath it.
        rmsKnobFade.setBypassed(!(rms&&rmsLayoutFade.amount()>=.999f),now);
        if(rms||rmsKnobFade.amount()<=.001f)rmsLayoutFade.setBypassed(!rms,now);
    }
    if(!rms)for(auto* child:rmsTime.getChildren())if(auto* value=dynamic_cast<CompressorValueLabel*>(child))value->cancel();
    trace.getProperties().set("rmsTrace",rms);applyDetectorAppearance();
    if(viewInitialized&&pageFade.presented()==2)layoutSidechainControls(advancedPanel.getBounds());
    const juce::String explanation=rms?"RMS averaging precedes the existing Attack response. RMS Time and Attack both affect onset; neither adds host latency. Old sessions retain legacy timing.":"Gain-envelope attack with finite minimum-attack transient overshoot. Old saved sessions retain their original detector/gain law. Type ms or seconds, for example 0.01 s.";
    if(attack.getTooltip()!=explanation){attack.setTooltip(explanation);attackL.setTooltip(explanation);for(auto* child:attack.getChildren())if(auto* value=dynamic_cast<juce::Label*>(child))value->setTooltip(explanation);}
}
void OpenCompressorAudioProcessorEditor::applyDetectorAppearance()
{
    const bool rms=p.parameters.getRawParameterValue("detectorMode")->load()>.5f;
    const bool visible=pageFade.presented()==2&&(rms||rmsKnobFade.amount()>.001f);
    const bool interactive=pageFade.interactive()&&!pagesSuspended;
    for(auto* c:std::initializer_list<juce::Component*>{&rmsTime,&rmsTimeL}){c->setVisible(visible);c->setAlpha(pageFade.alpha()*rmsKnobFade.amount());c->setInterceptsMouseClicks(interactive&&rms&&rmsKnobFade.amount()>.05f&&!rmsLayoutFade.active(),interactive&&rms&&rmsKnobFade.amount()>.05f&&!rmsLayoutFade.active());}
    sidechainPreview.setInterceptsMouseClicks(interactive&&!rmsLayoutFade.active(),false);
    if(rmsLayoutFade.active())sidechainPreview.endDrag();
    if(!trace.getProperties().contains("rmsBlend")||!juce::approximatelyEqual((float)trace.getProperties()["rmsBlend"],rmsTintFade.amount())){trace.getProperties().set("rmsBlend",rmsTintFade.amount());trace.repaint();}
}
void OpenCompressorAudioProcessorEditor::advanceDetectorAppearance(double timeMs)
{
    if(!displayActive)return;
    const double now=getPeer()!=nullptr?juce::Time::getMillisecondCounterHiRes():timeMs;
    bool changed=false;for(auto* fade:{&rmsLayoutFade,&rmsKnobFade,&rmsTintFade})changed=fade->advance(now)||changed;
    const bool rms=p.parameters.getRawParameterValue("detectorMode")->load()>.5f;
    if(pageFade.presented()==2){
        if(rms&&rmsLayoutFade.amount()>=.999f)rmsKnobFade.setBypassed(false,now);
        if(!rms&&rmsKnobFade.amount()<=.001f)rmsLayoutFade.setBypassed(true,now);
    }
    if(changed){applyDetectorAppearance();if(pageFade.presented()==2)layoutSidechainControls(advancedPanel.getBounds());}
}
void OpenCompressorAudioProcessorEditor::updateLookaheadAvailability(){juce::ScopedValueSetter<bool> guard(updatingLookahead,true);static constexpr double values[]{0,1,2,5,10,20};const double maximum=values[juce::jlimit(0,5,maxLookahead.getSelectedItemIndex())];lookahead.setNormalisableRange({0.0,juce::jmax(.01,maximum),.01,.7});lookahead.setValue(juce::jmin(maximum,(double)p.parameters.getRawParameterValue("lookahead")->load()),juce::dontSendNotification);const bool smoothEnabled=smooth.getToggleState(),lookaheadEnabled=smoothEnabled&&maximum>0;maxLookahead.setEnabled(smoothEnabled);lookahead.setEnabled(lookaheadEnabled);lookaheadL.setEnabled(lookaheadEnabled);const double stored=p.parameters.getRawParameterValue("lookahead")->load(),effective=smoothEnabled?juce::jmin(stored,maximum):0;
lookahead.setTooltip("Stored "+juce::String(stored,2)+" ms; effective detector lead "+juce::String(effective,2)+" ms. "+(smoothEnabled?maximum>0?"Drag to change lead within Max.":"Set Max above 0 ms.":"Enable Smooth to use Lookahead."));
maxLookahead.setTooltip("Host latency "+juce::String(p.getLatencySamples())+" samples ("+juce::String(1000.0*p.getLatencySamples()/p.spectrumSampleRate(),2)+" ms). Stored Max "+juce::String(maximum,0)+" ms. "+(smoothEnabled?"Latency is fixed by Max, not detector lead.":"Enable Smooth to apply Max."));
}

void OpenCompressorAudioProcessorEditor::paint(juce::Graphics& g){g.fillAll(T::bg);g.setColour(T::text);g.setFont(juce::FontOptions(18,juce::Font::bold));g.drawText("OPEN COMPRESSOR",18,7,210,25,juce::Justification::left);g.setColour(T::amber);g.fillRoundedRectangle(18,35,42,2.5f,1.25f);}
void OpenCompressorAudioProcessorEditor::resized(){
 const int w=getWidth(),h=getHeight();int x=w-18-366;
 const std::array<juce::Button*,7> buttons{&undo,&redo,&hoverHelp,&freeze,&detailsView,&sidechainView,&bypass};const std::array<int,7> widths{24,24,24,62,74,50,84};
 for(size_t i=0;i<buttons.size();++i){buttons[i]->setBounds(x,7,widths[i],28);x+=widths[i]+4;}
 display.setBounds(14,48,w-96,h-62);meters.setBounds(w-78,34,66,h-48);
 const auto overlay=display.getBounds();const int line24=overlay.getY()+juce::roundToInt(overlay.getHeight()*2.f/3);
 const juce::Rectangle<int> panel(overlay.getX()+22,line24-90,overlay.getWidth()-30,86);advancedPanel.setBounds(panel);
 if(pageFade.presented()==1){
    auto content=panel.reduced(10,0);auto utility=content.removeFromRight(174);content.removeFromRight(8);const int cell=content.getWidth()/6;
    for(auto pair:std::initializer_list<std::pair<juce::Slider*,juce::Label*>>{{&input,&inputL},{&knee,&kneeL},{&hold,&holdL},{&range,&rangeL},{&link,&linkL},{&lookahead,&lookaheadL}}){auto c=content.removeFromLeft(cell);pair.second->setBounds(c.getX(),c.getY()+7,c.getWidth(),11);pair.first->setTextBoxStyle(juce::Slider::TextBoxBelow,false,c.getWidth(),14);pair.first->setBounds(c.getX(),c.getY()+18,c.getWidth(),62);pair.first->updateText();}
    const int ux=utility.getX(),uy=panel.getY()+6,bw=85;
    smooth.setBounds(ux,uy,bw,21);autoGain.setBounds(ux+bw+4,uy,bw,21);maxLookahead.setBounds(ux,uy+25,bw,21);autoRelease.setBounds(ux+bw+4,uy+25,bw,21);
    snapshotA.setBounds(ux,uy+50,22,21);snapshotB.setBounds(ux+26,uy+50,22,21);copy.setBounds(ux+52,uy+50,66,21);kneeView.setBounds(ux+122,uy+50,52,21);
 }
 if(pageFade.presented()==2)layoutSidechainControls(panel);
 const int top=juce::jmax(overlay.getBottom()-145,line24+6);auto controls=juce::Rectangle<int>(overlay.getX()+8,top,overlay.getWidth()-16,overlay.getBottom()-11-top);const int cell=controls.getWidth()/6;
 auto c=controls.removeFromLeft(cell).reduced(6,0);thresholdL.setBounds(c.removeFromTop(15));thresholdKnob.setBounds(c.translated(-display.getX(),-display.getY()));
 for(auto pair:std::initializer_list<std::pair<juce::Slider*,juce::Label*>>{{&ratio,&ratioL},{&attack,&attackL},{&release,&releaseL},{&mix,&mixL},{&output,&outputL}}){auto area=controls.removeFromLeft(cell).reduced(6,0);placeKnob(area,*pair.first,*pair.second);}
 display.setControlsTop((advancedPanel.isVisible()?panel.getY():top)-display.getY());
 display.setBadgeGeometry(top-display.getY(),panel.getY()-display.getY());
 if(viewInitialized&&viewRevision==p.editorState.revision.load()){const auto oldW=p.editorState.width.exchange(w),oldH=p.editorState.height.exchange(h);if(oldW!=w||oldH!=h)p.editorStateChanged();}
}
void OpenCompressorAudioProcessorEditor::layoutSidechainControls(juce::Rectangle<int> panel)
{
    auto content=panel.reduced(10,0);auto routing=content.removeFromLeft(128);content.removeFromLeft(8);auto options=content.removeFromRight(74);content.removeFromRight(8);
    const int timeX=content.getRight()-54;
    rmsTimeL.setBounds(timeX,panel.getY()+7,54,11);rmsTime.setBounds(timeX,panel.getY()+18,54,62);
    content.removeFromRight(juce::roundToInt(62*rmsLayoutFade.amount()));
    externalSidechain.setBounds(routing.getX(),panel.getY()+6,60,21);sidechainHistoryView.setBounds(routing.getX()+64,panel.getY()+6,64,21);listen.setBounds(routing.getX(),panel.getY()+31,128,21);scStatus.setBounds(routing.getX()+2,panel.getY()+55,124,25);
    auto filters=content.removeFromLeft(108);content.removeFromLeft(8);
    for(auto pair:std::initializer_list<std::pair<juce::Slider*,juce::Label*>>{{&sideHighPass,&sideHighPassL},{&sideLowPass,&sideLowPassL}}){auto c=filters.removeFromLeft(54);pair.second->setBounds(c.getX(),panel.getY()+7,54,11);pair.first->setTextBoxStyle(juce::Slider::TextBoxBelow,false,54,14);pair.first->setBounds(c.getX(),panel.getY()+18,54,62);}
    sidechainPreview.setBounds(content.withTrimmedTop(6).withTrimmedBottom(6));detectorModeL.setBounds(options.getX(),panel.getY()+7,74,11);detectorMode.setBounds(options.getX(),panel.getY()+20,74,21);trace.setBounds(options.getX(),panel.getY()+49,74,21);
}

juce::Colour CompressorLookAndFeel::valueAccent(juce::Component& c)
{ if(auto* slider=c.findParentComponentOfClass<juce::Slider>()){
        const auto name=slider->getName();return name=="THRESHOLD"||name=="SC HP"?T::amber:name=="RMS TIME"?T::rms:T::lime;
    }return T::lime; }
void CompressorLookAndFeel::fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))valueStyle.observeEditor(editor);
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::fillEditor(g,w,h,T::bg);
    else juce::LookAndFeel_V4::fillTextEditorBackground(g,w,h,editor);
}
void CompressorLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::outlineEditor(g,w,h,editor,valueAccent(editor),T::grid);
    else juce::LookAndFeel_V4::drawTextEditorOutline(g,w,h,editor);
}
