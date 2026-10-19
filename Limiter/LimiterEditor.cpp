#include "LimiterEditor.h"
#include <LimiterArtwork.h>
#include <cmath>
#include <cstdlib>

class LimiterValueLabel final : public juce::Label
{
public:
    explicit LimiterValueLabel(juce::Slider& s):slider(s) {}
    void mouseDown(const juce::MouseEvent& e) override { moved=false;startY=e.position.y;start=slider.valueToProportionOfLength(slider.getValue());fine=e.mods.isShiftDown(); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(!e.mouseWasDraggedSinceMouseDown())return;
        if(!gesture)gesture=std::make_unique<juce::Slider::ScopedDragNotification>(slider);
        moved=true;
        if(fine!=e.mods.isShiftDown()){fine=e.mods.isShiftDown();startY=e.position.y;start=slider.valueToProportionOfLength(slider.getValue());}
        slider.setValue(slider.proportionOfLengthToValue(juce::jlimit(0.0,1.0,start+(startY-e.position.y)*(fine?.0005:.005))),juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent&) override { gesture.reset();if(!moved)showEditor(); }
    void mouseDoubleClick(const juce::MouseEvent&) override {showEditor();}
    void editorShown(juce::TextEditor* editor) override {juce::Label::editorShown(editor);notifyCeiling(true);}
    void editorAboutToBeHidden(juce::TextEditor* editor) override {juce::Label::editorAboutToBeHidden(editor);notifyCeiling(false);}
    ~LimiterValueLabel() override {notifyCeiling(false);gesture.reset();}
private:
    void notifyCeiling(bool editing)
    {if(slider.getName()=="CEILING")if(auto* history=slider.findParentComponentOfClass<LimiterDisplay>())history->setCeilingTextEditing(editing);}
    juce::Slider& slider;std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    double start=0;float startY=0;bool moved=false,fine=false;
};
namespace LT
{
const auto bg=juce::Colour::fromRGB(14,17,22),panel=juce::Colour::fromRGB(25,30,37),raised=juce::Colour::fromRGB(33,39,47);
const auto grid=juce::Colour::fromRGB(49,57,68),dim=juce::Colour::fromRGB(155,167,180),text=juce::Colour::fromRGB(238,237,232);
const auto red=juce::Colour::fromRGB(244,81,88);
const auto gold=juce::Colour::fromRGB(230,188,116),coral=juce::Colour::fromRGB(255,116,88);
}
LimiterLookAndFeel::LimiterLookAndFeel()
{
    // Preserve the supplied PNG's alpha mask. Decode and resize once, shared
    // across instances; the toolbar supplies the current animated ink color.
    static const auto artwork=juce::ImageFileFormat::loadFrom(LimiterArtwork::Wrench_png,LimiterArtwork::Wrench_pngSize)
        .rescaled(96,96,juce::Graphics::highResamplingQuality);
    wrenchIcon=artwork;
    setColour(juce::TextEditor::backgroundColourId,LT::panel);
    setColour(juce::TextEditor::textColourId,LT::text);
    setColour(juce::TextEditor::focusedOutlineColourId,LT::gold);
    setColour(juce::TextEditor::highlightColourId,LT::gold.withAlpha(.25f));
    setColour(juce::Slider::textBoxTextColourId,LT::text);
    setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId,LT::raised);
    setColour(juce::ComboBox::textColourId,LT::text);
    setColour(juce::ComboBox::outlineColourId,LT::grid);
    setColour(juce::PopupMenu::backgroundColourId,LT::panel);
    setColour(juce::PopupMenu::textColourId,LT::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,LT::gold.withAlpha(.3f));
}
void LimiterLookAndFeel::drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider&s)
{
    const auto c=juce::Point<float>(x+w*.5f,y+h*.5f);
    const bool compact=(bool)s.getProperties().getWithDefault("compactLink",false);
    const float stroke=compact?3.f:4.f,halo=compact?5.f:h<35?4.f:8.f,r=juce::jmin(w,h)*.33f,a=start+pos*(end-start);
    const auto accent=s.getName()=="CEILING"?LT::red:LT::gold;
    g.setColour(LT::panel.interpolatedWith(accent,.06f));g.fillEllipse(c.x-r-halo,c.y-r-halo,2*(r+halo),2*(r+halo));
    juce::Path base,active;base.addCentredArc(c.x,c.y,r,r,0,start,end,true);active.addCentredArc(c.x,c.y,r,r,0,start,a,true);
    g.setColour(LT::grid);g.strokePath(base,juce::PathStrokeType(stroke,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(s.getName()=="CEILING"?LT::red:LT::gold);g.strokePath(active,juce::PathStrokeType(stroke,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(LT::bg);g.fillEllipse(c.x-r+6,c.y-r+6,2*(r-6),2*(r-6));
    auto tip=c+juce::Point<float>(std::sin(a),-std::cos(a))*r*.56f;
    g.setColour(LT::text);g.drawLine({c,tip},2.f);
}
void LimiterLookAndFeel::drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float position,float,float,juce::Slider::SliderStyle,juce::Slider& slider)
{
    const float cx=x+8.f,top=(float)y,bottom=(float)(y+h);
    juce::ignoreUnused(w);
    const float trackX=cx+5.f;
    g.setColour(LT::gold.withAlpha(.30f));g.fillRoundedRectangle(trackX-1,top,2,bottom-top,1);
    g.setColour(LT::dim.withAlpha(.38f));for(int i=0;i<=4;++i){const float row=top+(bottom-top)*i/4.f;g.drawLine(trackX+7,row,trackX+11,row,.7f);}
    const auto handle=juce::Rectangle<float>(cx+4,position-13,58,26);g.setColour(LT::bg.withAlpha(.95f));g.fillRoundedRectangle(handle.expanded(2),5);g.setColour(LT::raised);g.fillRoundedRectangle(handle,4);
    valueStyle.paintValue(g,slider,handle,LT::bg,LT::gold);
    g.setColour(LT::gold);g.fillRoundedRectangle(handle.getX(),handle.getY()+5,2,14,1);g.setColour(LT::text);g.setFont(juce::FontOptions(11.f,juce::Font::bold));
    g.drawText(juce::String(slider.getValue(),2)+" dB",handle,juce::Justification::centred);
}
void LimiterLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)
{
    const auto bounds=b.getLocalBounds().toFloat().reduced(1);
    const bool header=(bool)b.getProperties().getWithDefault("header",false),footer=(bool)b.getProperties().getWithDefault("footer",false);
    const float hover=(float)b.getProperties().getWithDefault("barHover",over?1.f:0.f),lit=(float)b.getProperties().getWithDefault("barLit",b.getToggleState()?1.f:0.f);
    const float enabled=(float)b.getProperties().getWithDefault("barEnabled",b.isEnabled()?1.f:.35f),press=(float)b.getProperties().getWithDefault("barPress",down?1.f:0.f);
    auto fill=LT::raised.withAlpha((header||footer)?.88f*hover:1.f).interpolatedWith(LT::gold.withAlpha(.22f),lit);
    fill=fill.brighter(.08f*hover).darker(.12f*press);g.setColour(fill.withMultipliedAlpha(enabled));g.fillRoundedRectangle(bounds,(header||footer)?4.f:5.f);
    if(!header&&!footer){g.setColour(LT::grid);g.drawRoundedRectangle(bounds,5,1.f);}
}
int LimiterLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{return slider.getComponentID()=="gainControl"?15:juce::LookAndFeel_V4::getSliderThumbRadius(slider);}
void LimiterLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& button,bool over,bool down)
{
    const bool toolbar=(bool)button.getProperties().getWithDefault("header",false)||(bool)button.getProperties().getWithDefault("footer",false);
    const float lit=(float)button.getProperties().getWithDefault("barLit",button.getToggleState()?1.f:0.f),hover=(float)button.getProperties().getWithDefault("barHover",over?1.f:0.f);
    const float enabled=(float)button.getProperties().getWithDefault("barEnabled",button.isEnabled()?1.f:.35f);
    const auto ink=LT::dim.interpolatedWith(LT::text,juce::jmax(lit,hover*.6f)).withMultipliedAlpha(enabled);
    if(button.getComponentID()=="advancedControls"){
        const auto area=button.getLocalBounds().withSizeKeepingCentre(12,12);g.setColour(ink);
        g.drawImageWithin(wrenchIcon,area.getX(),area.getY(),area.getWidth(),area.getHeight(),juce::RectanglePlacement::centred,true);return;
    }
    const bool previous=button.getComponentID()=="presetPrevious",next=button.getComponentID()=="presetNext";
    if(previous||next){const auto centre=button.getLocalBounds().toFloat().getCentre();const float direction=previous?-1.f:1.f;
        juce::Path triangle;triangle.addTriangle(centre.x-direction*2.f,centre.y-3.5f,centre.x-direction*2.f,centre.y+3.5f,centre.x+direction*2.f,centre.y);
        g.setColour(ink);g.fillPath(triangle);return;}
    if(button.getComponentID()=="reset")
    {
        const auto centre=button.getLocalBounds().toFloat().getCentre();const float scale=.8f;
        juce::Path icon;icon.addCentredArc(centre.x,centre.y,4.4f*scale,4.4f*scale,0,.65f,5.7f,true);
        g.setColour(ink);g.strokePath(icon,juce::PathStrokeType(1.3f*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        const auto tip=centre+juce::Point<float>(std::sin(5.7f),-std::cos(5.7f))*4.4f*scale;
        juce::Path arrow;arrow.addTriangle(tip.x-2.5f*scale,tip.y+1.4f*scale,tip.x+2.1f*scale,tip.y+.5f*scale,tip.x+.5f*scale,tip.y-2.8f*scale);g.fillPath(arrow);return;
    }
    if(button.getButtonText()=="<"||button.getButtonText()==">"){g.setColour(ink);g.setFont(juce::FontOptions(11.f));g.drawText(button.getButtonText(),button.getLocalBounds(),juce::Justification::centred);return;}
    if(toolbar){g.setFont(juce::FontOptions(9.2f,juce::Font::bold));g.setColour(ink);g.drawText(button.getButtonText(),button.getLocalBounds(),juce::Justification::centred);return;}
    juce::LookAndFeel_V4::drawButtonText(g,button,over,down);
}
juce::Font LimiterLookAndFeel::getTextButtonFont(juce::TextButton& b,int){return juce::Font((bool)b.getProperties().getWithDefault("header",false)?juce::FontOptions(9.2f,juce::Font::bold):(bool)b.getProperties().getWithDefault("footer",false)?juce::FontOptions(9.2f,juce::Font::bold):juce::FontOptions(10.5f));}
juce::Font LimiterLookAndFeel::getPopupMenuFont(){return juce::Font(juce::FontOptions(10.5f));}
void LimiterLookAndFeel::drawToggleButton(juce::Graphics&g,juce::ToggleButton&b,bool over,bool down)
{
    const auto area=b.getLocalBounds().toFloat().reduced(1);const bool header=(bool)b.getProperties().getWithDefault("header",false),footer=(bool)b.getProperties().getWithDefault("footer",false);
    const float footerHover=(float)b.getProperties().getWithDefault("barHover",over||down?1.f:0.f),footerLit=(float)b.getProperties().getWithDefault("barLit",b.getToggleState()?1.f:0.f);
    const float enabled=(float)b.getProperties().getWithDefault("barEnabled",b.isEnabled()?1.f:.35f),press=(float)b.getProperties().getWithDefault("barPress",down?1.f:0.f);
    auto fill=LT::raised.withAlpha((header||footer)?.88f*footerHover:1.f).interpolatedWith(LT::gold.withAlpha(.22f),footerLit).brighter(.08f*footerHover).darker(.12f*press).withMultipliedAlpha(enabled);
    g.setColour(fill);
    const auto pair=b.getProperties().getWithDefault("tpPair",juce::String()).toString();
    if(pair.isNotEmpty()){
        juce::Path shape;const bool left=pair=="left";
        shape.addRoundedRectangle(area.getX(),area.getY(),area.getWidth(),area.getHeight(),5.f,5.f,left,!left,left,!left);
        g.fillPath(shape);
    }else g.fillRoundedRectangle(area,5);
    if(!header&&!footer){g.setColour(LT::grid);g.drawRoundedRectangle(area,5,1.f);}
    g.setColour(LT::dim.interpolatedWith(LT::text,juce::jmax(footerLit,footerHover*.6f)).withMultipliedAlpha(enabled));g.setFont(header?juce::FontOptions(9.2f,juce::Font::bold):footer?juce::FontOptions(9.2f,juce::Font::bold):juce::FontOptions(10.5f));g.drawText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred);
}
void LimiterLookAndFeel::drawComboBox(juce::Graphics&g,int w,int h,bool,int,int,int,int,juce::ComboBox& box)
{
    const bool header=(bool)box.getProperties().getWithDefault("header",false),footer=(bool)box.getProperties().getWithDefault("footer",false);
    const float hover=(float)box.getProperties().getWithDefault("barHover",box.isMouseOver(true)?1.f:0.f);
    g.setColour(LT::raised.withAlpha((header||footer)?.88f*hover:.9f));g.fillRoundedRectangle(.5f,.5f,w-1.f,h-1.f,4);
    if(!header&&!footer){g.setColour(LT::grid);g.drawRoundedRectangle(.5f,.5f,w-1.f,h-1.f,5,1.f);}
    juce::Path arrow;arrow.addTriangle(w-14.f,h*.44f,w-8.f,h*.44f,w-11.f,h*.60f);g.setColour(LT::gold);g.fillPath(arrow);
}
juce::Font LimiterLookAndFeel::getComboBoxFont(juce::ComboBox& box){return juce::Font((bool)box.getProperties().getWithDefault("header",false)?juce::FontOptions(9.2f,juce::Font::bold):(bool)box.getProperties().getWithDefault("footer",false)?juce::FontOptions(9.2f,juce::Font::bold):juce::FontOptions(10.5f));}
void LimiterLookAndFeel::drawLabel(juce::Graphics&g,juce::Label&l)
{
    valueStyle.paintLabel(g,l,LT::bg,valueAccent(l));
    if(l.isBeingEdited())return;g.setFont(l.getFont());
    // A small glyph shadow keeps floating text readable without a rectangular mask.
    if(dynamic_cast<juce::Slider*>(l.getParentComponent())||l.getComponentID().endsWith("Label"))
    {g.setColour(LT::bg.withAlpha(.95f));for(auto offset:{juce::Point<int>{-1,0},{1,0},{0,-1},{0,1}})g.drawFittedText(l.getText(),l.getLocalBounds().translated(offset.x,offset.y),l.getJustificationType(),1);}
    g.setColour(l.findColour(juce::Label::textColourId));g.drawFittedText(l.getText(),l.getLocalBounds(),l.getJustificationType(),1);
}

juce::Label* LimiterLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label=new LimiterValueLabel(slider);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId,LT::text);
    label->setFont(juce::FontOptions(slider.getProperties().getWithDefault("compactLink",false)?11.f:12.f));
    return label;
}
void LimiterLookAndFeel::positionComboBoxText(juce::ComboBox& box,juce::Label& label)
{
    const bool compact=box.getComponentID()=="loudnessView";
    label.setBounds(6,0,box.getWidth()-26,box.getHeight());
    juce::ignoreUnused(compact);
    label.setFont(getComboBoxFont(box));label.setJustificationType(juce::Justification::centred);
}
juce::Rectangle<int> LimiterLookAndFeel::getTooltipBounds(const juce::String&,juce::Point<int> position,juce::Rectangle<int> area)
{return juce::Rectangle<int>(position.x+12,position.y+20,320,68).constrainedWithin(area);}
void LimiterLookAndFeel::drawTooltip(juce::Graphics& g,const juce::String& text,int w,int h)
{
    g.setColour(LT::raised);g.fillRoundedRectangle(0,0,(float)w,(float)h,5);
    g.setColour(LT::text);g.setFont(juce::FontOptions(10.5f));
    g.drawFittedText(text,10,6,w-20,h-12,juce::Justification::centredLeft,4);
}
void LimiterLookAndFeel::drawCornerResizer(juce::Graphics& g,int w,int h,bool over,bool down)
{
    g.setColour(LT::dim.withAlpha(over||down?.65f:.25f));
    for(float offset:{4.f,8.f})g.drawLine(w-offset,h-3.f,w-3.f,h-offset,1.f);
}

LimiterGainSlider::LimiterGainSlider()
{
    addChildComponent(valueEditor);
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setInputRestrictions(0,"0123456789.,+ dDB");
    valueEditor.setColour(juce::TextEditor::backgroundColourId,LT::raised);
    valueEditor.setColour(juce::TextEditor::textColourId,LT::text);
    valueEditor.setColour(juce::TextEditor::focusedOutlineColourId,LT::gold);
    valueEditor.onReturnKey=[this]{finishEditing(true);};
    valueEditor.onEscapeKey=[this]{finishEditing(false);};
    valueEditor.onFocusLost=[this]{if(valueEditor.isVisible())finishEditing(true);};
}

void LimiterGainSlider::mouseDown(const juce::MouseEvent& e)
{
    if(!e.mods.isLeftButtonDown())return;
    dragStartY=e.position.y;
    dragStartHandleY=getPositionOfValue(getValue());
    fineDrag=e.mods.isShiftDown();
    dragging=true;
    juce::Slider::mouseDown(e); // Keep the attachment's host gesture intact.
}

void LimiterGainSlider::mouseDrag(const juce::MouseEvent& e)
{
    if(!dragging)return;
    if(fineDrag!=e.mods.isShiftDown())
    {
        dragStartY=e.position.y;
        dragStartHandleY=getPositionOfValue(getValue());
        fineDrag=e.mods.isShiftDown();
    }
    const float top=getPositionOfValue(getMaximum());
    const float bottom=getPositionOfValue(getMinimum());
    const float targetY=dragStartHandleY+(e.position.y-dragStartY)*(fineDrag?.1f:1.f);
    const double proportion=juce::jlimit(0.0,1.0,(double)(bottom-targetY)/juce::jmax(1.f,bottom-top));
    setValue(proportionOfLengthToValue(proportion),juce::sendNotificationSync);
}

void LimiterGainSlider::mouseUp(const juce::MouseEvent& e)
{
    if(dragging)juce::Slider::mouseUp(e);
    dragging=false;
}

void LimiterGainSlider::positionEditor()
{
    valueEditor.setBounds(12,juce::jlimit(0,juce::jmax(0,getHeight()-24),
        juce::roundToInt(getPositionOfValue(getValue()))-12),58,24);
}

void LimiterGainSlider::mouseDoubleClick(const juce::MouseEvent&)
{
    valueEditor.setText(juce::String(getValue(),2),false);
    positionEditor();
    valueEditor.setVisible(true);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void LimiterGainSlider::resized()
{
    juce::Slider::resized();
    if(valueEditor.isVisible())positionEditor();
}

void LimiterGainSlider::finishEditing(bool commit)
{
    const auto entry=valueEditor.getText();
    valueEditor.setVisible(false);
    if(commit)
    {juce::Slider::ScopedDragNotification gesture(*this);setValue(getValueFromText(entry),juce::sendNotificationSync);}
}

void LimiterGainSlider::cancelEditing() {finishEditing(false);if(dragging){const auto now=juce::Time::getCurrentTime();juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),{}, {},1,0,0,0,0,this,this,now,{},now,1,false);juce::Slider::mouseUp(event);dragging=false;}}
namespace
{
constexpr float graphTop=18.f,markedFloor=-27.f;
float badgeObstacleVisibility(const juce::Component& child)
{
    if(!child.isVisible())return 0;
    if(child.getProperties().contains("badgeObstacleOpacity"))return float(child.getProperties()["badgeObstacleOpacity"]);
    return child.getAlpha()>.01f?1.f:0.f;
}
float graphBottom(const juce::Component& c){return (float)c.getHeight();}
float markedBottom(const juce::Component& c){const auto* meter=dynamic_cast<const LimiterMeters*>(&c);const auto* history=dynamic_cast<const LimiterDisplay*>(&c);return (float)(meter?meter->getChartHeight():history?history->getChartHeight():c.getHeight())-28.f;}
float rawLevelY(const juce::Component& c,float db){return juce::jmap(db,markedFloor,0.f,markedBottom(c),graphTop);}
float levelY(const juce::Component& c,float db){return juce::jlimit(graphTop,graphBottom(c),rawLevelY(c,db));}
juce::String levelText(float value)
{if(value<=1.e-5f)return "-INF";const float db=juce::Decibels::gainToDecibels(value);return juce::String(std::abs(db)<.005f?0.f:db,2);}
juce::Colour inputColour(){return juce::Colour(0xffb4c8dc);}
juce::Colour outputColour(){return juce::Colour(0xff829acb);}
juce::Colour historyReductionColour(){return juce::Colour(0xffda4850);}
juce::ColourGradient loudnessGradient(const juce::Component& component,float end,float alpha=1.f)
{
    juce::ColourGradient gradient(LT::red.withAlpha(alpha),0,graphTop,outputColour().withAlpha(alpha),0,end,false);
    constexpr float reference=OpenLimiterAudioProcessor::targetLufs();
    for(const auto& stop:std::initializer_list<std::pair<float,juce::Colour>>{{reference+3,LT::red},{reference,juce::Colour(0xffffc46c)},{reference-4,juce::Colour(0xffffedca)},{reference-10,inputColour()}})
    {const double position=(rawLevelY(component,stop.first)-graphTop)/(end-graphTop);if(position>0&&position<1)gradient.addColour(position,stop.second.withAlpha(alpha));}
    return gradient;
}
juce::ColourGradient loudnessTraceGradient(const juce::Component& component,float opacity=1.f)
{
    auto gradient=loudnessGradient(component,markedBottom(component));
    for(int i=0;i<gradient.getNumColours();++i)
        gradient.setColour(i,gradient.getColour(i).interpolatedWith(juce::Colours::white,.10f).withAlpha(.72f*opacity));
    return gradient;
}
juce::ColourGradient loudnessShadeGradient(const juce::Component& component,float alpha,float warmth=0.f)
{
    const float end=markedBottom(component),start=rawLevelY(component,-24.f);
    auto colours=loudnessGradient(component,end,alpha);
    for(int i=0;i<colours.getNumColours();++i)
        colours.setColour(i,colours.getColour(i).interpolatedWith(juce::Colour(0xffe4d2ba),warmth).withAlpha(alpha));
    auto gradient=colours;
    gradient.setColour(gradient.getNumColours()-1,outputColour().withAlpha(0.f));
    // Finish the under-fill at the marked floor, without a shelf or tint in
    // the loudness readout. Keep the signal/meter scale itself unchanged.
    for(int i=0;i<12;++i)
    {
        const float t=i/12.f,y=start+t*(end-start);
        const double position=(y-graphTop)/(end-graphTop);
        const float opacity=1.f-t*t*(3.f-2.f*t);
        gradient.addColour(position,colours.getColourAtPosition(position).withAlpha(alpha*opacity));
    }
    return gradient;
}
juce::ColourGradient loudnessBarGradient(const juce::Component& component,float end)
{
    const float marked=markedBottom(component);
    auto gradient=loudnessGradient(component,end);
    if(end<=marked)return gradient;
    gradient.setColour(gradient.getNumColours()-1,outputColour().withAlpha(0.f));
    // The small unmarked continuation rolls out from 27, rather than ending
    // in a hard rectangle. Colour positions above 27 match the history.
    for(int i=0;i<8;++i)
    {
        const float t=i/8.f,y=marked+t*(end-marked);
        gradient.addColour((y-graphTop)/(end-graphTop),outputColour().withAlpha(1.f-t*t*(3.f-2.f*t)));
    }
    return gradient;
}
double parseValue(const juce::String& text,juce::Slider& slider)
{
    auto value=text.trim().toLowerCase().replace(juce::String::charToString(0x2212),"-").replaceCharacter(',','.');
    double multiplier=1;
    const auto name=slider.getName();
    if((bool)slider.getProperties().getWithDefault("compactLink",false))
    {if(value.endsWith("%"))value=value.dropLastCharacters(1).trim();}
    else if(name=="RELEASE"||name=="ATTACK"||name=="LOOKAHEAD")
    {
        if(value.endsWith("ms"))value=value.dropLastCharacters(2).trim();
        else if(value.endsWith("s")){multiplier=1000;value=value.dropLastCharacters(1).trim();}
    }
    else if(name=="LINK"&&value.endsWith("%"))value=value.dropLastCharacters(1).trim();
    else if(name=="CEILING")
    {for(auto suffix:{"dbtp","dbfs","db"})if(value.endsWith(suffix)){value=value.dropLastCharacters((int)strlen(suffix)).trim();break;}}
    else if(name=="GAIN"&&value.endsWith("db"))value=value.dropLastCharacters(2).trim();
    const char* begin=value.toRawUTF8();char* end=nullptr;const double parsed=std::strtod(begin,&end)*multiplier;
    return end==begin||*end!='\0'||!std::isfinite(parsed)?slider.getValue():juce::jlimit(slider.getMinimum(),slider.getMaximum(),parsed);
}
}
LimiterDisplay::LimiterDisplay(OpenLimiterAudioProcessor& x):p(x)
{
    motion.reset(0);motion.park();modeFade.reset((int)p.parameters.getRawParameterValue("loudnessMode")->load());
    for(auto& path:paintPaths)path.preallocateSpace(framesCount*8);
    reductionOpacity=p.parameters.getRawParameterValue("bypass")->load()>.5f?0.f:1.f;
    statusLabel.setComponentID("listenStatus");statusLabel.setFont(juce::FontOptions(9.5f,juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId,LT::gold);statusLabel.setJustificationType(juce::Justification::left);
    statusLabel.setBorderSize({});statusLabel.setInterceptsMouseClicks(false,false);addAndMakeVisible(statusLabel);
    updateListeningText(p.monitoring());statusOpacity=displayedMonitor?1.f:0.f;
    statusLabel.setAlpha(statusOpacity);statusLabel.setBounds(96,0,statusWidth,16);
    setOpaque(true);setComponentID("historyDisplay");
    setTooltip("Both views keep the same eight-second scale. Scroll moves history left at the original speed. Sweep starts at 32%, returns to 40%, and parks after an empty page. New audio restarts at 32%. FREEZE then drag to inspect a frame. The ceiling reference appears only while editing Ceiling.");
}
void LimiterDisplay::setActive(bool enabled)
{
    if(active==enabled)return;active=enabled;
    clock.reset();lastTickTime=-1;scrollFraction=0;
    lastFadeTime=-1;
    if(enabled){reductionOpacity=p.parameters.getRawParameterValue("bypass")->load()>.5f?0.f:1.f;loudnessOpacity=showLoudness?1.f:0.f;}
    if(!enabled){ceilingDragging=false;ceilingTextEditing=false;}
    // A native host can paint immediately after showing the editor, before
    // its first timer/vblank callback. Restore the queued history and camera
    // now so that paint never exposes the empty full-width GR baseline.
    if(enabled)tick(juce::Time::getMillisecondCounterHiRes());
    repaint();
}
void LimiterDisplay::setFrozen(bool enabled)
{
    if(enabled==frozen)return;
    if(enabled){if(!active||!validFrames)capture();refreshHistoryEdge();for(int i=0;i<3;++i)frozenLoudnessTail[(size_t)i]=displayLoudnessTail(i);}
    else{clock.reset();lastTickTime=-1;}
    frozen=enabled;inspect=-1;repaint();
}
void LimiterDisplay::capture(uint64_t selected)
{
    const auto epoch=p.historyEpoch(),sequence=selected==UINT64_MAX?p.historySequence():selected;
    const bool resetMotion=!motion.isInitialized()||epoch!=lastEpoch||p.historySequence()<lastSourceSequence;
    if(epoch!=lastEpoch||p.historySequence()<lastSourceSequence){frames={};reductionCurve={};signalDb={};loudnessCurve={};validFrames=0;retainIntegratedHistory=false;previousIdleLoudness.fill(-100.f);lastSequence=lastSourceSequence=silentFrames=0;lastEpoch=epoch;reductionBadges.reset(p.historyRate());reductionTrace.reset(p.historyRate());loudnessTrace.reset(p.historyRate());}
    if(sequence<=lastSourceSequence)return;
    const std::array<float,3> oldLoudness=validFrames?loudnessCurve.back():std::array<float,3>{-100.f,-100.f,-100.f};
    const int added=(int)juce::jmin<uint64_t>(sequence-lastSourceSequence,framesCount);
    // Reopening an already running instance restores its captured history.
    // A fresh/parked live view waits for an onset at the initial sweep point.
    if(resetMotion&&sequence>(uint64_t)std::ceil(p.historyRate()*.1))motion.reset(0);
    if(sequence-lastSourceSequence>framesCount){reductionBadges.reset(p.historyRate());reductionTrace.reset(p.historyRate());loudnessTrace.reset(p.historyRate());}
    if(added<framesCount){std::move(frames.begin()+added,frames.end(),frames.begin());std::move(reductionCurve.begin()+added,reductionCurve.end(),reductionCurve.begin());std::move(signalDb.begin()+added,signalDb.end(),signalDb.begin());std::move(loudnessCurve.begin()+added,loudnessCurve.end(),loudnessCurve.begin());}
    bool resumed=false;int retainedAdded=added;
    for(int n=0;n<added;++n)
    {
        auto& f=frames[(size_t)(framesCount-added+n)];f={};
        const auto frameSequence=sequence-(uint64_t)added+1+(uint64_t)n+silentFrames;
        p.readHistoryFrame(frameSequence-silentFrames,f);
        if(motion.mode()==1&&motion.isParked()&&std::max({f.raw,f.driven,f.output})>1.e-5f)
        {
            // Preserve the first incoming frame while clearing the empty page.
            const auto incoming=f;restartEmptySweep(oldLoudness);f=incoming;resumed=true;retainedAdded=added-n;
        }
        reductionCurve[(size_t)(framesCount-added+n)]=reductionTrace.push(f.reduction);
        signalDb[(size_t)(framesCount-added+n)]={juce::Decibels::gainToDecibels(f.raw,-100.f),juce::Decibels::gainToDecibels(f.output,-100.f)};
        for(int selection=0;selection<3;++selection)loudnessCurve[(size_t)(framesCount-added+n)][(size_t)selection]=loudnessTrace.push(selection,f.loudness[(size_t)selection]);
        if(auto* badge=reductionBadges.push(frameSequence,f.reduction,animationTime,reductionCurve[(size_t)(framesCount-added+n)]))
        {
            const int source=framesCount-added+n-(int)(frameSequence-badge->sequence);
            // Confirmation supplies 33 future frames, enough to clear a full
            // 46-pixel label at the smallest supported editor size.
            for(int i=juce::jmax(0,source-32);i<=juce::jmin(framesCount-added+n,source+32);++i)
                badge->clearanceReduction=juce::jmax(badge->clearanceReduction,reductionCurve[(size_t)i]);
        }
    }
    validFrames=juce::jmin(framesCount,validFrames+retainedAdded);lastSourceSequence=sequence;lastSequence=sequence+silentFrames;
    if(resetMotion&&!resumed){const double seconds=motion.preferredSeconds();motion.reset((double)validFrames/(seconds*p.historyRate()),seconds);if(motion.mode()==1&&!pageHasSignal())motion.park();}
}
bool LimiterDisplay::pageHasSignal() const noexcept
{
    const int first=juce::jmax(framesCount-validFrames,juce::jmax(0,(int)std::floor(framesCount-motion.fraction()*visibleFrames()+scrollFraction)-1));
    for(int i=first;i<framesCount;++i){const auto& f=frames[(size_t)i];if(std::max({f.raw,f.driven,f.output})>1.e-5f)return true;}
    return false;
}
void LimiterDisplay::restartEmptySweep(const std::array<float,3>& oldLoudness)
{
    // The live meter may already contain the new onset. Retain only the
    // previously presented empty page, never a newly measured live value.
    previousIdleLoudness=oldLoudness;
    historyAppearance=0;
    retainIntegratedHistory=std::isfinite(oldLoudness[0])&&oldLoudness[0]>-99.f;
    const std::array<float,3> held{retainIntegratedHistory?oldLoudness[0]:-100.f,-100.f,-100.f};
    frames={};reductionCurve={};signalDb.fill({-100.f,-100.f});loudnessCurve.fill(held);
    for(auto& frame:frames)frame.loudness=held;
    // Seed the silent time to the left of the initial sweep point. It stays
    // part of the scrolling history, rather than a temporary fade overlay.
    validFrames=retainIntegratedHistory?juce::jmin(framesCount,(int)std::ceil(LimiterHistoryMotion::startPosition*visibleFrames())):0;
    reductionBadges.reset(p.historyRate());reductionTrace.reset(p.historyRate());loudnessTrace.reset(p.historyRate());
    if(retainIntegratedHistory)loudnessTrace.push(0,held[0]);
    motion.restartAtStart();
}
void LimiterDisplay::appendSilence(int count)
{
    if(count<=0)return;
    const int added=juce::jmin(count,framesCount);
    if(added<framesCount){std::move(frames.begin()+added,frames.end(),frames.begin());std::move(reductionCurve.begin()+added,reductionCurve.end(),reductionCurve.begin());std::move(signalDb.begin()+added,signalDb.end(),signalDb.begin());std::move(loudnessCurve.begin()+added,loudnessCurve.end(),loudnessCurve.begin());}
    for(int n=0;n<added;++n){const auto index=(size_t)(framesCount-added+n);frames[index]={};
        signalDb[index]={-100.f,-100.f};
        reductionCurve[index]=reductionTrace.push(0);
        for(int selection=0;selection<3;++selection){const auto held=p.liveLoudness(selection);frames[index].loudness[(size_t)selection]=held;loudnessCurve[index][(size_t)selection]=loudnessTrace.push(selection,held);}
        reductionBadges.push(lastSequence+(uint64_t)count-(uint64_t)added+1+(uint64_t)n,0,animationTime,reductionCurve[index]);
    }
    silentFrames+=(uint64_t)count;lastSequence+=(uint64_t)count;validFrames=juce::jmin(framesCount,validFrames+added);
}
void LimiterDisplay::refreshHistoryEdge()
{
    // Preview already captured frames without advancing any filter or badge
    // state. Keep this preview when frozen, including after ring overwrite.
    edgeFrameCount=0;if(!validFrames||motion.isParked())return;
    auto trace=reductionTrace;
    for(int i=0;i<2;++i){if(!p.readHistoryFrame(lastSourceSequence+1+(uint64_t)i,edgeFrames[(size_t)i]))break;
        edgeReduction[(size_t)i]=trace.push(edgeFrames[(size_t)i].reduction);++edgeFrameCount;}
}
void LimiterDisplay::updateListeningText(int mode)
{
    displayedMonitor=mode;
    statusLabel.setText(mode==1?"LISTENING: DELTA":mode==2?"LISTENING: LEVEL COMPARE":"",juce::dontSendNotification);
    // Shape and measure only when the words change, not on every refresh.
    statusWidth=(int)std::ceil(statusLabel.getFont().getStringWidthFloat(statusLabel.getText()))+2;
}
bool LimiterDisplay::updateBadgeVisibility(double elapsedMs)
{
    // Scroll keeps its original pixel fade and appearance timing exactly.
    if(motion.mode()==0)return false;
    const float width=float(getWidth());if(width<=0)return false;
    LimiterReductionBadges::Projection offsets{};
    bool returning=motion.isReturning();
    if(!frozen&&!motion.isParked()){
        auto forecast=motion;
        for(int i=1;i<=LimiterReductionBadges::projectionSteps;++i){
            forecast.advance(LimiterReductionBadges::projectionStepSeconds);
            returning|=forecast.isReturning();
            const double seconds=i*double(LimiterReductionBadges::projectionStepSeconds);
            offsets[size_t(i)]=float(width*(forecast.fraction()-motion.fraction()-seconds/visibleSeconds()));
        }
    }
    return reductionBadges.updateVisibility(elapsedMs,[&](const auto& badge){
        if(!returning)return 1.f;
        if(badge.sequence>lastSequence||lastSequence-badge.sequence>framesCount+64)return 1.f;
        const float x=historyX(float(framesCount-1-int(lastSequence-badge.sequence)));
        const float y=levelY(*this,-badge.clearanceReduction)+14;
        const auto annotation=juce::Rectangle<float>(x-23,y-5,46,21);
        float opacity=LimiterReductionBadges::projectedClearance(annotation.getX(),annotation.getRight(),-width,0,offsets);
        for(auto* child:getChildren())if(badgeObstacleVisibility(*child)>0&&child->getComponentID()!="gainControl"){
            const auto bounds=child->getBounds().toFloat().expanded(3);
            if(annotation.getBottom()<=bounds.getY()||annotation.getY()>=bounds.getBottom())continue;
            const auto clearance=LimiterReductionBadges::projectedClearance(annotation.getX(),annotation.getRight(),bounds.getX(),bounds.getRight(),offsets);
            opacity=std::min(opacity,LimiterReductionBadges::obstacleVisibility(clearance,badgeObstacleVisibility(*child)));
        }
        return opacity;
    });
}
void LimiterDisplay::tick(double now)
{
    if(!active)return;
    bool dirty=loudnessTailDirty&&!frozen;loudnessTailDirty=false;
    animationTime=now;
    // Animate opacity independently of the history clock, including in Freeze.
    // A reversal starts from the current opacity instead of snapping.
    const float target=p.parameters.getRawParameterValue("bypass")->load()>.5f?0.f:1.f;
    const float step=(float)juce::jlimit(0.,180.,lastFadeTime<0?0.:now-lastFadeTime)/180.f;
    const float next=reductionOpacity<target?juce::jmin(target,reductionOpacity+step):juce::jmax(target,reductionOpacity-step);
    if(next!=reductionOpacity){reductionOpacity=next;dirty=true;}
    const float loudnessTarget=showLoudness?1.f:0.f;
    const float loudnessNext=loudnessOpacity<loudnessTarget?juce::jmin(loudnessTarget,loudnessOpacity+step):juce::jmax(loudnessTarget,loudnessOpacity-step);
    if(loudnessNext!=loudnessOpacity){loudnessOpacity=loudnessNext;dirty=true;}
    dirty|=modeFade.advance((int)p.parameters.getRawParameterValue("loudnessMode")->load(),step*180.);
    lastFadeTime=now;
    for(const auto& badge:reductionBadges.accepted())if(badge.sequence&&LimiterReductionBadges::opacity(badge,now)<1.f){dirty=true;break;}
    if(!frozen)
    {
        const auto previous=lastSequence;const auto oldFraction=scrollFraction;const auto oldHead=motion.fraction();const auto source=p.historySequence();
        const bool restart=lastTickTime<0||now<lastTickTime||p.historyEpoch()!=lastEpoch;
        const auto position=clock.advance(source,p.historyEpoch(),now,p.historyRate(),p.historyBufferFrames());
        if(restart||source-lastSourceSequence>192){
            capture((uint64_t)position);scrollFraction=(float)(position-std::floor(position));
        }else{
            // One presentation clock for real and silent frames. Consume the
            // queued audio first, then fill any missing frames immediately.
            // Keeping the fraction across this handoff avoids a pause or jump.
            const double elapsed=juce::jlimit(0.,1000.*framesCount/p.historyRate(),now-lastTickTime);
            const double advance=scrollFraction+elapsed*p.historyRate()/1000;
            const int count=(int)advance;
            const auto available=source>=lastSourceSequence?source-lastSourceSequence:0;
            const int actual=(int)juce::jmin<uint64_t>((uint64_t)count,available);
            if(actual>0)capture(lastSourceSequence+(uint64_t)actual);
            appendSilence(count-actual);scrollFraction=(float)(advance-count);
        }
        if(!motion.isInitialized()){const double seconds=motion.preferredSeconds();motion.reset((double)validFrames/(seconds*p.historyRate()),seconds);}
        motion.setPageHasSignal(pageHasSignal());
        if(!restart)motion.advance(juce::jlimit(0.,visibleSeconds(),(now-lastTickTime)*.001));
        if(previous!=lastSequence||std::abs(scrollFraction-oldFraction)>.0001f||std::abs(motion.fraction()-oldHead)>1.e-6)dirty=true;
        refreshHistoryEdge();
    }
    lastTickTime=now;
    const auto fade=[&](float& opacity,float targetValue){const float before=opacity;
        opacity=opacity<targetValue?juce::jmin(targetValue,opacity+step):juce::jmax(targetValue,opacity-step);
        if(opacity!=before)dirty=true;};
    fade(historyAppearance,1.f);
    const float head=(float)motion.fraction();
    const bool boundaryVisible=head*getWidth()>1
        &&(motion.mode()==1?!motion.isParked():head*getWidth()<getWidth()-1);
    if(boundaryVisible)lastBoundary=head;
    fade(boundaryOpacity,boundaryVisible?1.f:0.f);
    fade(ceilingOpacity,isCeilingEditing()?1.f:0.f);
    fade(frozenOpacity,frozen?1.f:0.f);
    const int requestedMonitor=p.monitoring();
    if(statusOpacity==0&&displayedMonitor!=requestedMonitor){
        updateListeningText(requestedMonitor);
        dirty=true;
    }
    // Retain the outgoing words until transparent. Reversals in the same
    // mode continue from their current alpha rather than flashing new text.
    fade(statusOpacity,requestedMonitor!=0&&requestedMonitor==displayedMonitor?1.f:0.f);
    if(statusOpacity==0&&displayedMonitor!=requestedMonitor){
        updateListeningText(requestedMonitor);
        dirty=true;
    }
    statusLabel.setAlpha(statusOpacity);
    statusLabel.setBounds(frozenOpacity>0?164:96,0,statusWidth,16);
    if(inspect>=0)lastInspect=inspect;
    fade(inspectionOpacity,frozen&&inspect>=0?1.f:0.f);
    dirty|=updateBadgeVisibility(step*180.);
    // Publish one complete presentation state, not a tail repaint followed by
    // several whole-chart invalidations during the same native refresh.
    if(dirty)repaint();
}
float LimiterDisplay::ceilingY() const{return levelY(*this,p.parameters.getRawParameterValue("ceiling")->load());}
void LimiterDisplay::paint(juce::Graphics& g)
{
    const bool bypass=p.parameters.getRawParameterValue("bypass")->load()>.5f;
    g.fillAll(LT::bg);if(!frozen&&getPeer()==nullptr){capture();refreshHistoryEdge();}
    const float width=(float)getWidth(),floor=graphBottom(*this),fraction=scrollFraction;
    const float head=(float)motion.fraction()*width,scale=(float)(width/visibleFrames());
    const float edgeEnd=head;
    const int historyEnd=juce::jlimit(0,getWidth(),(int)std::ceil(edgeEnd));
    const int firstVisible=juce::jmax(framesCount-validFrames,juce::jmax(0,(int)std::floor(framesCount-head/scale+fraction)-2));

    for(int step=0;step<=9;++step){const float y=levelY(*this,-3.f*step);g.setColour(LT::dim.withAlpha(.15f));g.drawLine(0,y,width,y,.6f);}
    // Scale numerals sit below the history layers, with no sign or unit suffix.
    g.setFont(juce::FontOptions(10.f));g.setColour(LT::dim.withAlpha(.8f));
    for(int i=0;i<=9;++i)g.drawText(juce::String(3*i),getWidth()-36,juce::roundToInt(rawLevelY(*this,-3.f*i))-7,29,14,juce::Justification::right);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    // Only the future side of the old page fades away. The GR baseline and
    // retained integrated history on the left are drawn once at full opacity.
    if(historyAppearance<1.f)
    {
        juce::Graphics::ScopedSaveState future(g);
        g.reduceClipRegion({historyEnd,0,getWidth()-historyEnd,getHeight()});
        const float opacity=1.f-historyAppearance;
        g.setColour(historyReductionColour().withMultipliedAlpha(opacity*reductionOpacity));g.drawLine(0,graphTop,width,graphTop,1.5f);
        const int selection=loudnessDisplayMode();const float held=previousIdleLoudness[(size_t)selection];
        if(held>-99&&loudnessOpacity>0){const float y=rawLevelY(*this,held);
            g.setGradientFill(loudnessShadeGradient(*this,.03f*opacity*loudnessOpacity*modeFade.opacity()));g.fillRect(0.f,y,width,floor-y);
            g.setGradientFill(loudnessTraceGradient(*this,opacity*loudnessOpacity*modeFade.opacity()));g.drawLine(0,y,width,y,1.15f);}
    }
    for(auto& path:paintPaths)path.clear();
    auto& reductionEdge=paintPaths[4];
    {juce::Graphics::ScopedSaveState state(g);g.reduceClipRegion({0,(int)graphTop-1,historyEnd,(int)(floor-graphTop)+1});
     auto& inputEdge=paintPaths[0];auto& outputEdge=paintPaths[1];auto& inputFill=paintPaths[2];auto& outputFill=paintPaths[3];
     bool first=true;float firstX=0,previousX=0,previousGr=graphTop;
     const int endIndex=framesCount+edgeFrameCount;
     const auto grAt=[&](int i){return rawLevelY(*this,-(i<framesCount?reductionCurve[(size_t)i]:edgeReduction[(size_t)(i-framesCount)]));};
     for(int i=firstVisible;i<endIndex;++i)
     {
         // Two queued real points straddle the fractional boundary. Without
         // enough queued data, the final real point itself meets the boundary.
         const float x=edgeFrameCount<2&&i==endIndex-1?edgeEnd:historyX((float)i);
         const auto levels=i<framesCount?signalDb[(size_t)i]:std::array<float,2>{juce::Decibels::gainToDecibels(edgeFrames[(size_t)(i-framesCount)].raw,-100.f),juce::Decibels::gainToDecibels(edgeFrames[(size_t)(i-framesCount)].output,-100.f)};
         const float in=rawLevelY(*this,levels[0]),out=rawLevelY(*this,levels[1]),gr=grAt(i);
         if(first){firstX=x;inputEdge.startNewSubPath(x,in);outputEdge.startNewSubPath(x,out);inputFill.startNewSubPath(x,juce::jlimit(graphTop,floor,in));outputFill.startNewSubPath(x,juce::jlimit(graphTop,floor,out));
             if(x>0){reductionEdge.startNewSubPath(0,graphTop);reductionEdge.lineTo(x,graphTop);reductionEdge.lineTo(x,gr);}
             else reductionEdge.startNewSubPath(x,gr);first=false;}
         else{inputEdge.lineTo(x,in);outputEdge.lineTo(x,out);inputFill.lineTo(x,juce::jlimit(graphTop,floor,in));outputFill.lineTo(x,juce::jlimit(graphTop,floor,out));
             const float previous=i>=2?grAt(i-2):previousGr;
             const float next=i+1<endIndex?grAt(i+1):gr;
             const auto controls=LimiterLoudnessSpline::controls(previous,previousGr,gr,next);const float third=(x-previousX)/3;
             reductionEdge.cubicTo(previousX+third,controls.first,x-third,controls.second,x,gr);}
         previousX=x;previousGr=gr;
     }
     if(!first){inputFill.lineTo(previousX,floor);inputFill.lineTo(firstX,floor);inputFill.closeSubPath();
         outputFill.lineTo(previousX,floor);outputFill.lineTo(firstX,floor);outputFill.closeSubPath();}
     else{reductionEdge.startNewSubPath(0,graphTop);reductionEdge.lineTo(edgeEnd,graphTop);}
     const auto signalFill=[&](juce::Graphics& target){
         target.setGradientFill({outputColour().withAlpha(.39f*historyAppearance),0,graphTop,outputColour().withAlpha(.025f*historyAppearance),0,floor,false});target.fillPath(outputFill);
         target.setGradientFill({inputColour().withAlpha(.30f*historyAppearance),0,graphTop,inputColour().withAlpha(.018f*historyAppearance),0,floor,false});target.fillPath(inputFill);};
     const bool fading=head<width-.5f&&head>0;
     const float fadeStart=fading?juce::jmax(0.f,head-12.f):head;
     const auto fadeAlpha=[](float position){const float u=juce::jlimit(0.f,1.f,position);return 1.f-.25f*u*u*(3.f-2.f*u);};
     if(fading){const int first=(int)std::floor(fadeStart);
         {juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion({0,0,first,getHeight()});signalFill(g);}
         // Rasterise only this tiny, immutable strip once. Re-filling a whole
         // history path for every fade column multiplies tessellation work.
         const float pixelScale=juce::jmax(1.f,g.getInternalContext().getPhysicalPixelScaleFactor());
         const int stripWidth=historyEnd-first,usedPixels=juce::jmax(1,(int)std::ceil(stripWidth*pixelScale));
         // Keep the two integer-width cases separately. Reusing exact-size
         // surfaces also avoids a new subsection image wrapper on each draw.
         auto& strip=historyFadeStrips[(size_t)(stripWidth>12)];
         const int height=juce::jmax(1,(int)std::ceil(getHeight()*pixelScale));
         if(strip.getWidth()!=usedPixels||strip.getHeight()!=height)strip=juce::Image(juce::Image::ARGB,usedPixels,height,true);
         else strip.clear(strip.getBounds());
         if(fadeColumnAlpha.size()<(size_t)usedPixels)fadeColumnAlpha.resize((size_t)usedPixels);
         {juce::Graphics target(strip);target.addTransform({pixelScale,0,-first*pixelScale,0,pixelScale,0});signalFill(target);}
         for(int x=0;x<usedPixels;++x)fadeColumnAlpha[(size_t)x]=fadeAlpha((first+(x+.5f)/pixelScale-fadeStart)/(head-fadeStart));
         {juce::Image::BitmapData pixels(strip,juce::Image::BitmapData::readWrite);
          for(int y=0;y<pixels.height;++y){auto* row=reinterpret_cast<juce::PixelARGB*>(pixels.getLinePointer(y));
              for(int x=0;x<usedPixels;++x)row[x].multiplyAlpha(fadeColumnAlpha[(size_t)x]);}}
         g.setColour(juce::Colours::white);g.drawImage(strip,{(float)first,0,(float)stripWidth,(float)getHeight()});}
     else signalFill(g);
     const auto edgeGradient=[&](juce::Colour colour,float opacity){juce::ColourGradient gradient{colour.withAlpha(opacity),fadeStart,0,colour.withAlpha(opacity*.75f),head,0,false};
         for(double position:{.25,.5,.75})gradient.addColour(position,colour.withAlpha(opacity*fadeAlpha((float)position)));return gradient;};
     if(fading)g.setGradientFill(edgeGradient(outputColour(),.75f*historyAppearance));
     else g.setColour(outputColour().withAlpha(.75f*historyAppearance));g.strokePath(outputEdge,juce::PathStrokeType(.9f));
     if(fading)g.setGradientFill(edgeGradient(inputColour(),.57f*historyAppearance));
     else g.setColour(inputColour().withAlpha(.57f*historyAppearance));g.strokePath(inputEdge,juce::PathStrokeType(.75f));
     auto& reductionShade=paintPaths[5];reductionShade=reductionEdge;
     reductionShade.lineTo(first?edgeEnd:previousX,graphTop);reductionShade.lineTo(firstVisible<framesCount?juce::jmin(0.f,historyX((float)firstVisible)):0.f,graphTop);reductionShade.closeSubPath();
     g.setGradientFill({historyReductionColour().withAlpha(.27f*historyAppearance),0,graphTop,historyReductionColour().withAlpha(.07f*historyAppearance),0,floor,false});g.fillPath(reductionShade);
     // Give the zero-reduction stroke its full thickness above the chart edge.
     g.setColour(historyReductionColour().withMultipliedAlpha(reductionOpacity));
     g.strokePath(reductionEdge,juce::PathStrokeType(1.5f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
     if(loudnessOpacity*modeFade.opacity()>0.f)
     {
         const int selection=loudnessDisplayMode();
         const float appearance=(selection==0&&retainIntegratedHistory?1.f:historyAppearance)*modeFade.opacity();
         const float tail=displayLoudnessTail(selection);
         std::array<float,framesCount+1> levels{};levels.fill(-100.f);
         const int firstIndex=firstVisible;
         for(int i=firstIndex;i<framesCount;++i)
         {
             const float captured=loudnessCurve[(size_t)i][(size_t)selection];
             levels[(size_t)i]=captured;
         }
         levels.back()=tail;
         const float curveScale=(float)((width-2.f)/visibleFrames());
         auto& trace=paintPaths[6];auto& shade=paintPaths[7];bool started=false;float firstX=0,lastX=0;
         const auto closeShade=[&]{if(started){shade.lineTo(lastX,floor);shade.lineTo(firstX,floor);shade.closeSubPath();}};
         for(int i=firstIndex;i<=framesCount;++i)
         {
             if(levels[(size_t)i]<=-99){closeShade();started=false;continue;}
             // Reserve two pixels for a flat join: the last visible pixel must
             // reach the meter level even during a steep final transition.
             const float x=head-2.f-(i==framesCount?0.f:(framesCount-i+fraction)*curveScale),y=rawLevelY(*this,levels[(size_t)i]);
             if(!started){trace.startNewSubPath(x,y);shade.startNewSubPath(x,y);firstX=x;started=true;}
             else
             {
                 const float previous=i-2>=firstIndex&&levels[(size_t)(i-2)]>-99?levels[(size_t)(i-2)]:levels[(size_t)(i-1)];
                 const float next=i<framesCount&&levels[(size_t)(i+1)]>-99?levels[(size_t)(i+1)]:levels[(size_t)i];
                 const auto controls=LimiterLoudnessSpline::controls(previous,levels[(size_t)(i-1)],levels[(size_t)i],next);
                 const float third=(x-lastX)/3.f,c1=rawLevelY(*this,controls.first),c2=rawLevelY(*this,controls.second);
                 trace.cubicTo(lastX+third,c1,x-third,c2,x,y);shade.cubicTo(lastX+third,c1,x-third,c2,x,y);
             }
             lastX=x;
         }
         if(started){const float endY=rawLevelY(*this,levels.back());trace.lineTo(edgeEnd,endY);shade.lineTo(edgeEnd,endY);lastX=edgeEnd;}
         closeShade();g.setGradientFill(loudnessShadeGradient(*this,.03f*loudnessOpacity*appearance));g.fillPath(shade);
         g.setGradientFill(loudnessTraceGradient(*this,loudnessOpacity*appearance));
         g.strokePath(trace,juce::PathStrokeType(1.15f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
     }
    }
    if(ceilingOpacity>0){g.setColour(juce::Colour(0xffff8d93).withAlpha(ceilingOpacity*(bypass?.25f:1.f)));g.drawLine(0,ceilingY(),head,ceilingY(),1.2f);}
    // Feathered charcoal tint gives the floating gain track its own contrast.
    g.setGradientFill({LT::panel.withAlpha(.88f),0,0,LT::panel.withAlpha(0.f),82,0,false});g.fillRect(0.f,graphTop-2.f,82.f,floor-graphTop+2.f);
    {juce::Graphics::ScopedSaveState badgeClip(g);g.reduceClipRegion({0,(int)graphTop,(int)std::ceil(head),(int)(floor-graphTop)});
     // Confirmed event labels stay attached to their original peak sequence.
     g.setFont(juce::FontOptions(9.f,juce::Font::bold));float lastTagRight=-100;
     for(size_t n=0;n<reductionBadges.accepted().size();++n)
     {
         const auto& badge=reductionBadges.ordered(n);
         if(!badge.sequence||badge.sequence>lastSequence||lastSequence-badge.sequence>framesCount+64)continue;
         const int i=framesCount-1-(int)(lastSequence-badge.sequence);
         const float reduction=badge.reduction,x=historyX((float)i);
         const float labelY=levelY(*this,-badge.clearanceReduction)+14;
         const auto tag=juce::Rectangle<float>(x-23,labelY,46,16);
         const auto annotation=tag.getUnion({x-3,labelY-5,6,5});
         if(tag.getRight()<=0||tag.getX()>=width||tag.getBottom()>floor)continue;
         float clearance=1.f;
         for(auto* child:getChildren())if(badgeObstacleVisibility(*child)>0&&child->getComponentID()!="gainControl")
         {
             const auto bounds=child->getBounds().toFloat().expanded(3);
             if(annotation.getBottom()<=bounds.getY()||annotation.getY()>=bounds.getBottom())continue;
             const float gap=std::max(bounds.getX()-annotation.getRight(),annotation.getX()-bounds.getRight());
             clearance=std::min(clearance,LimiterReductionBadges::obstacleVisibility(LimiterReductionBadges::clearanceOpacity(gap),badgeObstacleVisibility(*child)));
         }
         if(tag.getX()<lastTagRight+24)continue;lastTagRight=tag.getRight();
         const float badgeClearance=motion.mode()==0?clearance:std::min(clearance,badge.obstructionOpacity);
         const float opacity=.82f*badgeClearance*historyAppearance*(getPeer()==nullptr?1.f:LimiterReductionBadges::opacity(badge,animationTime));
         g.setGradientFill({LT::gold.withAlpha(0.f),0,0,LT::gold.withAlpha(opacity),82,0,false});g.fillRoundedRectangle(tag,3);
         juce::Path arrow;arrow.startNewSubPath(x,labelY-5);arrow.lineTo(x-3,labelY);arrow.lineTo(x+3,labelY);arrow.closeSubPath();g.fillPath(arrow);
         g.setGradientFill({LT::bg.withAlpha(0.f),0,0,LT::bg.withAlpha(.35f*opacity),82,0,false});g.drawRoundedRectangle(tag,3,.6f);
         g.setGradientFill({LT::bg.withAlpha(0.f),0,0,LT::bg.withAlpha(opacity),82,0,false});g.drawText("-"+juce::String(reduction,1)+" dB",tag,juce::Justification::centred);
      }
    }
    if(boundaryOpacity>0){
        // A fractional hairline alternates between one bright pixel and two
        // dim ones. Whole physical pixels keep its weight steady while the
        // underlying history/camera retain their continuous positions.
        const float pixelScale=juce::jmax(1.f,g.getInternalContext().getPhysicalPixelScaleFactor());
        const float markerWidth=juce::jmax(1.f,std::round(.75f*pixelScale))/pixelScale;
        const float right=std::ceil(lastBoundary*width*pixelScale)/pixelScale;
        const auto ink=LT::bg.interpolatedWith(juce::Colour(0xffb8c2d2),.45f*.75f/markerWidth);
        g.setColour(ink.withAlpha(boundaryOpacity));g.fillRect(right-markerWidth,graphTop,markerWidth,floor-graphTop);
    }
    if(frozenOpacity>0)
    {
        g.setColour(LT::gold.withMultipliedAlpha(frozenOpacity));g.setFont(juce::FontOptions(9.5f,juce::Font::bold));g.drawText("FROZEN",96,0,62,16,juce::Justification::left);
    }
    if(inspectionOpacity>0&&lastInspect>=0)
        {
            const int index=juce::jlimit(0,framesCount-1,lastInspect);const auto& f=frames[(size_t)index];const float x=historyX((float)index);
            g.setColour(LT::text.withAlpha(.6f*inspectionOpacity));g.drawLine(x,graphTop,x,floor,1);g.setFont(juce::FontOptions(9.5f,juce::Font::bold));
            const int textX=statusOpacity>0?statusLabel.getRight()+8:164;
            g.drawText(juce::String((framesCount-1-index)/p.historyRate(),2)+" s ago   IN "+levelText(f.raw)+"   OUT "+levelText(f.output)+"   GR "+juce::String(f.reduction,2)+" dB",textX,0,juce::jmax(0,getWidth()-textX-4),16,juce::Justification::left);
        }
    g.setColour(LT::bg);g.fillRect(getWidth()-1,(int)graphTop,1,(int)(floor-graphTop));
}
void LimiterDisplay::mouseDown(const juce::MouseEvent& e)
{if(frozen&&e.position.y<graphBottom(*this)){inspect=inspectionIndex(e.position.x);repaint();}}
void LimiterDisplay::mouseDrag(const juce::MouseEvent& e)
{if(frozen){inspect=inspectionIndex(e.position.x);repaint();}}
int LimiterDisplay::inspectionIndex(float x) const noexcept
{
    if(x<0||x>=(float)motion.fraction()*getWidth()||getWidth()<=0)return -1;
    const int index=juce::roundToInt(framesCount+(x-motion.fraction()*getWidth())*visibleFrames()/getWidth()+scrollFraction);
    return index>=framesCount-validFrames&&index<framesCount?index:-1;
}
void LimiterDisplay::mouseMove(const juce::MouseEvent&)
{setMouseCursor(frozen?juce::MouseCursor::CrosshairCursor:juce::MouseCursor::NormalCursor);}
void LimiterDisplay::mouseExit(const juce::MouseEvent&){setMouseCursor(juce::MouseCursor::NormalCursor);}
float LimiterMeters::mapLevel(float db,float top,float marked,float bottom) noexcept
{return juce::jlimit(top,bottom,juce::jmap(db,-27.f,0.f,marked,top));}
void LimiterMeters::tick(double now)
{
    const float amount=lastTime<0||now<lastTime||now-lastTime>250?1.f:(float)(1.-std::exp(-(now-lastTime)/24.));
    for(int i=0;i<3;++i){const float value=p.liveLoudness(i);
        animatedLoudness[(size_t)i]=value<-99||animatedLoudness[(size_t)i]<-99?value:animatedLoudness[(size_t)i]+amount*(value-animatedLoudness[(size_t)i]);}
    if(lastTime<0||now<lastTime)lraMotion.reset();
    const float low=p.loudnessRangeLow(),high=p.loudnessRangeHigh();
    lraMotion.advance(low,high,p.measuredSeconds()>=3&&low>-99.f&&high>-99.f,lastTime<0?0.:now-lastTime);
    lastTime=now;repaint();
}
void LimiterMeters::mouseDown(const juce::MouseEvent& e)
{
    const float x=e.position.x+scaleGutter,width=(float)getLogicalWidth();
    if(x>=width)return;
    if(e.position.y<graphTop&&x<width*.48f)p.resetPeakHolds();
    else if(x>width*.60f&&e.position.y<markedBottom(*this)+52)
    {const float u=width/270.f;int selection=history.loudnessBarSelection(0);
     if(x>220*u)selection=history.loudnessBarSelection(2);else if(x>207*u)selection=history.loudnessBarSelection(1);
     if(selection!=(int)p.parameters.getRawParameterValue("loudnessMode")->load()){auto* parameter=p.parameters.getParameter("loudnessMode");parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1((float)selection));parameter->endChangeGesture();}}
}
void LimiterMeters::paint(juce::Graphics& g)
{
    g.fillAll(LT::bg);const float top=graphTop,bottom=graphBottom(*this),marked=markedBottom(*this),u=getLogicalWidth()/270.f,loudBottom=juce::jmin(bottom,marked+12.f);
    g.addTransform(juce::AffineTransform::translation(-(float)scaleGutter,0));
    const auto label=[&](const juce::String& text,float x,float y,float w,float h,juce::Colour colour,float font)
    {g.setColour(colour);g.setFont(juce::FontOptions(font));g.drawText(text,juce::Rectangle<float>(x*u,y,w*u,h),juce::Justification::centred);};
    // The OUT origin is the same integer boundary as the history canvas.
    // No part of the current loudness join starts at the scale numerals.
    const auto stripX=[&](float x){return x>=50.f&&x<=77.f?scaleGutter+(x-50.f)*u:x*u;};
    // Carry the history grid to the loudness strip, keeping stereo tracks clear.
    {juce::Graphics::ScopedSaveState clip(g);
     for(float x:{50.f,95.f})g.excludeClipRegion({(int)std::floor(stripX(x)),(int)top,(int)std::ceil(stripX(x)+27.f*u)-(int)std::floor(stripX(x)),getHeight()-(int)top});
     g.setColour(LT::dim.withAlpha(.15f));
     for(int step=0;step<=9;++step){const float y=rawLevelY(*this,-3.f*step);g.drawLine((float)scaleGutter,y,177*u,y,.6f);}}
    const bool truePeak=p.parameters.getRawParameterValue("truePeakMeter")->load()>.5f;
    const auto held=truePeak?p.truePeakLevel():juce::jmax(p.outputPeak(0),p.outputPeak(1));
    label(levelText(held),40,0,47,16,held>1?LT::red:truePeak?juce::Colour(0xff99c940):inputColour(),12);
    label((p.maximumReduction()<.005f?juce::String("0.00"):"-"+juce::String(p.maximumReduction(),2)),86,0,44,16,LT::coral,12);
    const int mode=history.loudnessDisplayMode();const float modeAlpha=history.loudnessModeOpacity(),lufs=displayLoudness(mode);
    const auto live=[&](int selection){return displayLoudness(selection);};
    // The rail is a single horizontal live level; all changes begin inside
    // history. Slightly different, very light tints distinguish the two areas.
    if(history.loudnessTraceOpacity()*modeAlpha>0.f&&live(mode)>-99)
    {
        juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion({(int)scaleGutter,(int)top,juce::jmax(1,(int)std::ceil(177*u)-scaleGutter),juce::jmax(1,(int)std::ceil(marked)-(int)top)});
        // Break both the trace and its under-fill across each stereo strip,
        // including the gap between its channels. Resume in the rail gaps.
        for(float x:{50.f,95.f})
        {
            const int left=(int)std::floor(stripX(x)),right=(int)std::ceil(stripX(x)+27.f*u);
            g.excludeClipRegion({left,(int)top,right-left,getHeight()-(int)top});
        }
        const float historyY=rawLevelY(*this,live(mode));
        juce::Path trace,shade;trace.startNewSubPath((float)scaleGutter,historyY);trace.lineTo(177*u,historyY);
        shade.startNewSubPath((float)scaleGutter,marked);shade.lineTo((float)scaleGutter,juce::jlimit(top,marked,historyY));
        shade.lineTo(177*u,juce::jlimit(top,marked,historyY));shade.lineTo(177*u,marked);shade.closeSubPath();
        g.setGradientFill(loudnessShadeGradient(*this,.045f*history.loudnessTraceOpacity()*modeAlpha,.65f));g.fillPath(shade);
        g.setGradientFill(loudnessTraceGradient(*this,history.loudnessTraceOpacity()*modeAlpha));g.strokePath(trace,juce::PathStrokeType(1.f));
    }
    label("Max M  "+(p.maximumLoudness(2)<-99?juce::String("--"):juce::String(p.maximumLoudness(2),1)),150,1,109,16,LT::dim,9);
    const auto strip=[&](float x,float width,float db,juce::Colour colour,bool reduction,float end,float opacity=1.f)
    {
        x=stripX(x);width*=u;const auto track=juce::Rectangle<float>(x,top,width,end-top);g.setColour(colour.withAlpha(.07f));g.fillRect(track);
        const float y=mapLevel(reduction?-db:db,top,marked,end);const auto fill=reduction?juce::Rectangle<float>(x,top,width,y-top):juce::Rectangle<float>(x,y,width,end-y);
        juce::ColourGradient gradient(colour.brighter(.20f),x,top,colour,x,end,false);
        if(x>170*u)gradient=loudnessBarGradient(*this,end);
        gradient.multiplyOpacity(opacity);g.setGradientFill(gradient);if(reduction)g.setColour(colour.withAlpha(.9f));g.fillRect(fill);
        if((!reduction&&x<=170*u&&y<end)||(reduction&&db>.001f)){g.setColour(colour.brighter(.18f).withMultipliedAlpha(opacity));g.fillRect(x,y-.5f,width,1.f);}
    };
    g.setColour(LT::bg);g.fillRect((int)std::floor(stripX(50.f)),(int)top,1,(int)(bottom-top));
    for(int c=0;c<2;++c)
    {
        const float x=50+c*15;
        const float peak=levelY(*this,juce::Decibels::gainToDecibels(p.outputLevel(c),-100.f));
        g.setGradientFill({inputColour().withAlpha(.55f),stripX(x),peak,inputColour().withAlpha(.25f),stripX(x),bottom,false});g.fillRect(stripX(x),peak,12*u,bottom-peak);
        strip(x,12,juce::Decibels::gainToDecibels(p.outputRms(c),-100.f),outputColour(),false,bottom);
        if(peak<bottom){g.setColour(inputColour());g.fillRect(stripX(x),peak-.7f,12*u,1.4f);}
        const float hold=rawLevelY(*this,p.outputMarkerPeak(c));
        if(hold<bottom){g.setColour(held>1?LT::red:outputColour().withAlpha(.6f));g.fillRect(stripX(x),juce::jmax(top,hold),12*u,.6f);}
        strip(95+c*15,12,p.reductionLevel(c),LT::red,true,bottom);
        const float maxReduction=p.reductionPeak(c),maxY=rawLevelY(*this,-maxReduction);
        if(maxReduction>.005f&&maxY<bottom){g.setColour(LT::coral.withAlpha(.6f));g.fillRect(stripX(95+c*15),juce::jmax(top,maxY),12*u,.6f);}
    }
    const char* names[]{"I","S","M"};
    const int primary=history.loudnessBarSelection(0),second=history.loudnessBarSelection(1),third=history.loudnessBarSelection(2);
    const float primaryAlpha=history.loudnessBarOpacity(0),secondAlpha=history.loudnessBarOpacity(1),thirdAlpha=history.loudnessBarOpacity(2);
    strip(177,30,live(primary),LT::gold,false,loudBottom,primaryAlpha);strip(213,6,live(second),inputColour(),false,loudBottom,secondAlpha);strip(226,6,live(third),inputColour(),false,loudBottom,thirdAlpha);
    const auto loudnessHold=[&](float x,float width,int selection,float opacity)
    {
        if(selection==0)return; // Integrated is an accumulated reading.
        const float maximum=p.loudnessPeak(selection);
        if(maximum<-99.f)return;
        const float y=mapLevel(maximum,top,marked,loudBottom);
        if(y>=loudBottom)return;
        // Sample the same gradient (including its floor roll-off) as the bar.
        const auto colour=loudnessBarGradient(*this,loudBottom).getColourAtPosition((y-top)/(loudBottom-top));
        g.setColour(colour.withMultipliedAlpha(.75f*opacity));g.fillRect(x*u,y,width*u,.6f);
    };
    loudnessHold(177,30,primary,primaryAlpha);loudnessHold(213,6,second,secondAlpha);loudnessHold(226,6,third,thirdAlpha);
    label(names[primary],177,loudBottom+2,30,12,LT::text.withMultipliedAlpha(primaryAlpha),9);label(names[second],210,loudBottom+2,12,12,LT::dim.withMultipliedAlpha(secondAlpha),8);label(names[third],223,loudBottom+2,12,12,LT::dim.withMultipliedAlpha(thirdAlpha),8);
    if(mode==0&&p.measuredSeconds()>=3&&p.loudnessRangeLow()>-99)
    {
        const float high=mapLevel(lraMotion.high(p.loudnessRangeHigh()),top,marked,marked),low=mapLevel(lraMotion.low(p.loudnessRangeLow()),top,marked,marked);
        g.setColour(LT::gold.withMultipliedAlpha(modeAlpha));g.drawLine(165*u,high,165*u,low,1.8f);g.drawLine(165*u,high,171*u,high,1.8f);g.drawLine(165*u,low,171*u,low,1.8f);
        const float centre=juce::jlimit(top+16,loudBottom-20,(high+low)*.5f);
        label("LRA",132,centre-14,30,13,LT::gold.withMultipliedAlpha(modeAlpha),9);label(juce::String(p.loudnessRange(),1),132,centre,30,13,LT::gold.withMultipliedAlpha(modeAlpha),10);
    }
    for(int i=0;i<=9;++i)
    {
        const float y=rawLevelY(*this,-3.f*i);g.setColour(LT::dim);g.setFont(juce::FontOptions(10.f));
        g.drawText(juce::String(3*i),(int)(243*u),juce::roundToInt(y)-7,(int)(25*u),14,juce::Justification::left);
    }
    const auto numericColour=lufs<-99?LT::dim:loudnessGradient(*this,marked).getColourAtPosition(juce::jlimit(0.,1.,(double)(rawLevelY(*this,lufs)-top)/(marked-top)));
    label(lufs<-99?"-INF":juce::String(lufs,2),143,loudBottom+18,114,33,numericColour.withMultipliedAlpha(modeAlpha),28);label("LUFS",176,loudBottom+50,48,12,LT::dim,9);
    const int seconds=(int)p.measuredSeconds();label(p.isLoudnessPaused()?"Paused":juce::String(seconds/60)+":"+juce::String(seconds%60).paddedLeft('0',2),132,loudBottom+50,40,12,LT::dim,8);
}
void LimiterDetailsBackground::paint(juce::Graphics& g)
{
    g.fillAll(LT::bg);g.setColour(LT::grid.withAlpha(.5f));g.drawHorizontalLine(0,12.f,(float)getWidth()-12.f);
    if(auto* editor=getParentComponent())for(auto* id:{"truePeakControl","delta","historyMotion","latencyReadout"})if(auto* c=editor->findChildWithID(id))
        g.drawVerticalLine(c->getX()-4,7.f,(float)getHeight()-7.f);
}
OpenLimiterAudioProcessorEditor::OpenLimiterAudioProcessorEditor(OpenLimiterAudioProcessor& x)
:AudioProcessorEditor(&x),p(x),display(x),meters(x,display)
{
    lnf.setPopupMenuOwner(*this);setOpaque(true);setLookAndFeel(&lnf);setWantsKeyboardFocus(true);setResizable(true,true);setResizeLimits(720,440,1400,900);
    addAndMakeVisible(display);addAndMakeVisible(meters);meters.setComponentID("meterRail");
    addAndMakeVisible(detailBackdrop);detailBackdrop.setInterceptsMouseClicks(false,false);detailBackdrop.setComponentID("advancedDock");
    addAndMakeVisible(latencyLabel);latencyLabel.setColour(juce::Label::textColourId,LT::dim);latencyLabel.setInterceptsMouseClicks(false,false);
    latencyLabel.setFont(juce::FontOptions(8.5f));
    latencyLabel.setComponentID("latencyReadout");latencyLabel.setJustificationType(juce::Justification::right);latencyLabel.setBorderSize({});
    setupKnob(drive,driveLabel,"GAIN"," dB",0);drive.setSliderStyle(juce::Slider::LinearVertical);drive.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);drive.setSliderSnapsToMousePosition(false);
    setupKnob(ceiling,ceilingLabel,"CEILING"," dBFS",0);setupKnob(lookahead,lookaheadLabel,"LOOKAHEAD"," ms",.180);
    setupKnob(attack,attackLabel,"ATTACK"," ms",275);setupKnob(release,releaseLabel,"RELEASE"," ms",400);
    setupKnob(transientLink,transientLinkLabel,"PEAKS","%",75);setupKnob(releaseLink,releaseLinkLabel,"RECOVERY","%",100);
    transientLink.setComponentID("transientLinkControl");releaseLink.setComponentID("releaseLinkControl");
    transientLinkLabel.setComponentID("transientLinkLabel");releaseLinkLabel.setComponentID("releaseLinkLabel");
    for(auto* knob:{&transientLink,&releaseLink})knob->getProperties().set("compactLink",true);
    for(auto* label:{&transientLinkLabel,&releaseLinkLabel})label->setFont(juce::FontOptions(8.f,juce::Font::bold));
    linkGroupLabel.setText("CHANNEL LINKING",juce::dontSendNotification);linkGroupLabel.setComponentID("linkGroupLabel");
    linkGroupLabel.setJustificationType(juce::Justification::centred);linkGroupLabel.setColour(juce::Label::textColourId,LT::dim);
    linkGroupLabel.setFont(juce::FontOptions(8.f,juce::Font::bold));linkGroupLabel.setInterceptsMouseClicks(false,false);display.addAndMakeVisible(linkGroupLabel);
    for(auto* control:std::initializer_list<juce::Component*>{&drive,&driveLabel,&ceiling,&ceilingLabel,&lookahead,&lookaheadLabel,&attack,&attackLabel,&release,&releaseLabel,&transientLink,&transientLinkLabel,&releaseLink,&releaseLinkLabel})display.addAndMakeVisible(*control);

    for(auto item:std::initializer_list<std::pair<const char*,juce::Slider*>>{{"drive",&drive},{"ceiling",&ceiling},{"lookahead",&lookahead},{"attack",&attack},{"release",&release},{"transientLink",&transientLink},{"releaseLink",&releaseLink}})
    {
        sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,item.first,*item.second));
        auto* slider=item.second;
        slider->valueFromTextFunction=[slider](const juce::String& text){return parseValue(text,*slider);};
        slider->textFromValueFunction=[slider](double value){return juce::String(std::abs(value)<.005?0.:value,slider->getName()=="RELEASE"||slider->getName()=="ATTACK"?1:2);};slider->updateText();
    }
    // L 2's Attack travel follows a fourth-power curve. Change only the editor
    // mapping so existing normalized host automation retains its 5.0 meaning.
    attack.setNormalisableRange(juce::NormalisableRange<double>(0,10000,.001,.25));
    attack.setTextValueSuffix("");
    attack.textFromValueFunction=[](double value)
    {return value>=1000?juce::String(value/1000,3)+" s":juce::String(value,value<10?3:2)+" ms";};
    attack.updateText();
    lookahead.textFromValueFunction=[](double value){return juce::String(value,3)+" ms";};lookahead.setTextValueSuffix("");lookahead.updateText();
    for(auto* knob:{&transientLink,&releaseLink}){knob->textFromValueFunction=[](double value){return juce::String(value,1);};knob->updateText();}
    ceiling.onDragStart=[this]{display.setCeilingDragging(true);};
    ceiling.onDragEnd=[this]{display.setCeilingDragging(false);};
    ceiling.onValueChange=[this]{if(display.isCeilingEditing())display.repaint();};
    drive.setDoubleClickReturnValue(false,0);
    const auto choice=[&](juce::ComboBox& box,const juce::StringArray& options,const char* parameter,const char* id)
    {box.addItemList(options,1);box.setComponentID(id);addAndMakeVisible(box);choices.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,parameter,box));};
    instant.setComponentID("zeroInstant");instant.setButtonText("Instant");instant.getProperties().set("footer",true);display.addAndMakeVisible(instant);instant.setAlpha(0);instant.setInterceptsMouseClicks(false,false);
    instant.setTooltip("Experimental Zero mode: no lookahead, no audio delay. Oversampling and TP limiting are bypassed; TP metering remains available. Abrupt peaks can distort. Saved with the session, independently of presets.");
    toggles.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"zeroLatency",instant));
    updateStyleMenu();
    mode.setComponentID("modeControl");addAndMakeVisible(mode);
    styleAttachment=std::make_unique<juce::ParameterAttachment>(*p.parameters.getParameter("styleSelect"),
        [this](float value){mode.setSelectedId(juce::roundToInt(value)+1,juce::dontSendNotification);updateStyleControls(juce::roundToInt(value));},&p.undoManager);
    mode.onChange=[this]{if(mode.getSelectedId()>0){
        // Commit pending host automation before opening the UI undo gesture,
        // then commit this gesture so an immediate Undo sees the right value.
        p.parameters.copyState();
        styleAttachment->setValueAsCompleteGesture((float)(mode.getSelectedId()-1));
        p.parameters.copyState();
    }};
    display.addAndMakeVisible(mode);modeLabel.setComponentID("modeLabel");modeLabel.setText("STYLE",juce::dontSendNotification);modeLabel.setFont(juce::FontOptions(9.f,juce::Font::bold));modeLabel.setColour(juce::Label::textColourId,LT::dim);modeLabel.setJustificationType(juce::Justification::left);modeLabel.setBorderSize({});modeLabel.setInterceptsMouseClicks(false,false);display.addAndMakeVisible(modeLabel);
    choice(loudnessView,{"Integrated","Short term","Momentary"},"loudnessMode","loudnessView");
    choice(outputQuality,{"OS: Off","OS: 2x","OS: 4x","OS: 8x"},"outputQuality","outputQuality");
    choice(renderQuality,{"Render: Playback","Render: Min 2x","Render: Min 4x","Render: 8x"},"renderQuality","renderQuality");
    historyView.addItemList({"Scroll","Sweep"},1);historyView.setComponentID("historyMotion");
    historyView.getProperties().set("footer",true);historyView.setColour(juce::ComboBox::textColourId,LT::dim);addAndMakeVisible(historyView);
    historyView.onChange=[this]{const int selection=juce::jlimit(0,1,historyView.getSelectedId()-1);
        p.editorState.historyView=selection;display.setHistoryView(selection);p.editorStateChanged();};
    historyView.setTooltip("Scroll: original eight-second right-to-left view. Sweep: slower eleven-second view with a thin boundary, blank future area and smooth scroll-back. Switching eases the time scale without clearing captured audio.");
    bypass.setComponentID("bypassControl");bypass.setButtonText("BYPASS");addAndMakeVisible(bypass);
    truePeak.setComponentID("truePeakControl");truePeak.setButtonText("True Peak");addAndMakeVisible(truePeak);
    truePeakMeter.setComponentID("truePeakMeterControl");truePeakMeter.setButtonText("Meter");addAndMakeVisible(truePeakMeter);
    truePeak.getProperties().set("tpPair","left");truePeakMeter.getProperties().set("tpPair","right");
    toggles.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"bypass",bypass));
    toggles.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"truePeak",truePeak));
    toggles.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"truePeakMeter",truePeakMeter));
    for(auto* button:std::initializer_list<juce::TextButton*>{&undo,&redo,&help,&freeze,&reset,&pause,&slotA,&slotB,&copy,&delta,&compare,&advanced})
    {addAndMakeVisible(button);button->setColour(juce::TextButton::textColourOffId,LT::dim);button->setColour(juce::TextButton::textColourOnId,LT::text);button->setWantsKeyboardFocus(false);button->setComponentID(button->getButtonText().toLowerCase().replace(" ",""));}
    advanced.setComponentID("advancedControls");advanced.setTooltip("Show or hide Style, Lookahead, Attack, Release and Channel Linking. Gain and Ceiling remain visible. Saved with the session.");
    for(auto* button:{&help,&freeze,&pause,&delta,&compare,&advanced})button->setClickingTogglesState(true);
    advanced.onClick=[this]{setAdvancedControls(advanced.getToggleState());p.editorState.advancedControls=advancedShown;p.editorStateChanged();};
    undo.onClick=[this]{stopListening();p.undoManager.undo();};redo.onClick=[this]{stopListening();p.undoManager.redo();};
    help.onClick=[this]{setHelp(help.getToggleState());};
    freeze.onClick=[this]{display.setFrozen(freeze.getToggleState());};
    reset.onClick=[this]{p.resetLoudness();};reset.onPopup=[this]{showResetOptions();};pause.onClick=[this]{p.pauseLoudness(pause.getToggleState());};
    slotA.onClick=[this]{p.selectSnapshot(0);};slotB.onClick=[this]{p.selectSnapshot(1);};copy.onClick=[this]{p.copySnapshot();};
    delta.onClick=[this]{p.setMonitor(delta.getToggleState()?1:0);compare.setToggleState(false,juce::dontSendNotification);};
    compare.onClick=[this]{p.setMonitor(compare.getToggleState()?2:0);delta.setToggleState(false,juce::dontSendNotification);};
    bypass.onClick=[this]{stopListening();};
    truePeak.onClick=[this]{updateReadouts();};
    drive.setTooltip("Input gain. Drag the floating handle; Shift is fine. Double-click its value to type dB.");
    ceiling.setTooltip("Set the level where limiting begins and the maximum output level; Shift is fine. Click the value to type. A temporary reference line appears while editing. True Peak includes reconstructed peaks.");
    release.setTooltip("Envelope recovery time. Longer release can reduce modulation; shorter release recovers sooner. Values accept ms or s.");
    attack.setTooltip("Attack of the slower release stage. Fast values engage sustained reduction sooner; slow values leave more work to the short transient stage. The ceiling stays protected. Values accept ms or s.");
    lookahead.setTooltip("Preview time for the transient stage, from 0 to 5 ms. The style's existing audio delay stays constant while you adjust it. Double-click restores 0.180 ms.");
    transientLink.setTooltip("Peak linking. 100% applies shared L/R transient reduction; lower values allow the channels to react independently. True Peak output correction is independent per channel.");
    releaseLink.setTooltip("Recovery linking. 100% shares the slower gain envelope between L/R; lower values allow independent recovery. Double-click restores 100%.");
    mode.setTooltip("Nine limiting styles (Zero has a 0.35 ms core and an experimental Instant option), ordered from longest to shortest core delay. Oversampling processes the entire limiter. The footer reports the delay of the active path. Style changes prepare and warm a replacement, then fade through the timing change.");
    freeze.setTooltip("Freeze only the history picture. Drag to inspect recorded frame peaks. Audio and loudness measurement continue.");
    reset.setTooltip("Reset LUFS, loudness maxima/range and measured time. Right-click for reset-on-playback. Click held readings above the output/GR bars to reset peak holds separately.");
    pause.setTooltip("Pause loudness accumulation while audio and peak meters continue. Resume continues the same measurement.");
    delta.setButtonText("Delta");delta.setTooltip("Toggle to hear driven input minus limited output. Click again or press Escape to return to normal. Stays active when the editor closes or loses focus. Offline exports ignore this.");
    compare.setTooltip("Compare uses 3 s of measured raw/limited RMS energy and only turns output down. Meters show normal output. Internal Bypass plays the raw reference and clears Compare. Compare stays active when the editor closes or loses focus.");
    truePeak.setTooltip("Protect reconstructed peaks in the final output. The footer reports actual processing delay. Bypass disables protection.");
    truePeakMeter.setTooltip("Measure reconstructed peaks above OUT, without changing audio or latency. Enabling TP Limit also enables Meter; you can then turn Meter off independently. Off shows sample peaks.");
    outputQuality.setTooltip("Oversample the final output correction. Higher quality uses more CPU and adds displayed delay. Unchanged audio retains an exact delayed baseline.");
    renderQuality.setTooltip("Minimum output quality for offline export. Never lowers playback quality. Real-time exports use Playback.");
    bypass.setTooltip("Soft bypass to exact delayed raw input. Keeps all reported delay; output protection is bypassed too.");
    undo.setTooltip("Undo parameter edit (Command-Z).");redo.setTooltip("Redo parameter edit (Command-Shift-Z).");help.setTooltip("Optional hover help. Saved per instance; defaults off.");
    slotA.setTooltip("Recall A. Style, oversampling and True Peak stay shared to keep latency stable.");slotB.setTooltip("Recall B. Style, oversampling and True Peak stay shared to keep latency stable.");copy.setTooltip("Copy active sound settings into the other comparison slot.");
    freeze.setButtonText("Freeze");help.setButtonText("?");undo.setButtonText("<");redo.setButtonText(">");copy.setButtonText("Copy");compare.setButtonText("Compare");delta.setButtonText("Delta");reset.setButtonText("");pause.setButtonText("II");bypass.setButtonText("Bypass");truePeak.setButtonText("TP Limit");
    for(auto* button:{&undo,&redo,&help,&freeze,&slotA,&slotB,&copy,&advanced})button->getProperties().set("header",true);
    bypass.getProperties().set("header",true);
    for(auto* control:std::initializer_list<juce::Component*>{&truePeak,&truePeakMeter,&delta,&compare,&pause,&reset,&trace,&outputQuality,&renderQuality,&loudnessView}){
        control->getProperties().set("header",false);control->getProperties().set("footer",true);
        if(auto* button=dynamic_cast<juce::TextButton*>(control)){button->setColour(juce::TextButton::textColourOffId,LT::dim);button->setColour(juce::TextButton::textColourOnId,LT::text);}
    }
    for(auto* box:{&outputQuality,&renderQuality,&loudnessView})box->setColour(juce::ComboBox::textColourId,LT::dim);
    trace.setButtonText("Loudness");trace.setComponentID("loudnessTrace");trace.setClickingTogglesState(true);trace.setToggleState(true,juce::dontSendNotification);addAndMakeVisible(trace);trace.onClick=[this]{p.editorState.loudnessTrace=trace.getToggleState();display.setLoudnessVisible(trace.getToggleState());p.editorStateChanged();};trace.setTooltip("Show the selected I, S or M loudness reading as a smooth colored curve in the history.");
    styleAttachment->sendInitialUpdate();
    for(auto* button:{&presetPrevious,&presetButton,&presetNext}){addAndMakeVisible(button);button->getProperties().set("header",true);button->setWantsKeyboardFocus(false);}
    presetPrevious.setButtonText("Previous preset");presetNext.setButtonText("Next preset");
    presetPrevious.setComponentID("presetPrevious");presetButton.setComponentID("presetSelect");presetNext.setComponentID("presetNext");
    presetPrevious.onClick=[this]{stepPreset(-1);};presetNext.onClick=[this]{stepPreset(1);};presetButton.onClick=[this]{showPresets();};
    presetButton.setTooltip("Factory and user presets. Open, save, rename or delete a user preset. * marks changed sound settings.");
    presetPrevious.setTooltip("Previous preset");presetNext.setTooltip("Next preset");
    ready=true;restoreView();setDisplayActive(true);
}
OpenLimiterAudioProcessorEditor::~OpenLimiterAudioProcessorEditor()
{lnf.dismissPopupMenus();ceiling.hideTextBox(true);ready=false;stopTimer();setDisplayActive(false);drive.cancelEditing();tooltips.reset();setLookAndFeel(nullptr);}
void OpenLimiterAudioProcessorEditor::setupKnob(juce::Slider& slider,juce::Label& label,const juce::String& name,const juce::String& suffix,double def)
{
    slider.setName(name);slider.setComponentID(name.toLowerCase()+"Control");slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,20);slider.setTextValueSuffix(suffix);slider.setDoubleClickReturnValue(true,def);
    slider.setMouseDragSensitivity(180);addAndMakeVisible(slider);
    label.setComponentID(name.toLowerCase()+"Label");label.setText(name,juce::dontSendNotification);label.setJustificationType(juce::Justification::centred);label.setColour(juce::Label::textColourId,LT::dim);label.setFont(juce::FontOptions(9.f,juce::Font::bold));addAndMakeVisible(label);
}
void OpenLimiterAudioProcessorEditor::restoreView()
{
    delta.setToggleState(p.monitoring()==1,juce::dontSendNotification);compare.setToggleState(p.monitoring()==2,juce::dontSendNotification);drive.cancelEditing();freeze.setToggleState(false,juce::dontSendNotification);display.setFrozen(false);
    viewRevision=p.editorState.revision;setSize(p.editorState.width,p.editorState.height);
    p.editorState.details=true;
    const int view=juce::jlimit(0,1,p.editorState.historyView.load());historyView.setSelectedId(view+1,juce::dontSendNotification);display.setHistoryView(view);
    slotA.setToggleState(p.getSnapshot()==0,juce::dontSendNotification);slotB.setToggleState(p.getSnapshot()==1,juce::dontSendNotification);
    trace.setToggleState(p.editorState.loudnessTrace,juce::dontSendNotification);display.setLoudnessVisible(p.editorState.loudnessTrace);
    help.setToggleState(p.editorState.help,juce::dontSendNotification);
    setHelp(p.editorState.help);resized();updateReadouts();
    setAdvancedControls(p.editorState.advancedControls,true);
}
void OpenLimiterAudioProcessorEditor::setAdvancedControls(bool shown,bool immediate)
{
    advancedShown=shown;advanced.setToggleState(shown,juce::dontSendNotification);
    if(immediate)advancedProgress=shown?1.f:0.f;
    if(!shown){mode.hidePopup();for(auto* knob:{&lookahead,&attack,&release,&transientLink,&releaseLink})knob->hideTextBox(true);}
    applyAdvancedVisibility();
}
void OpenLimiterAudioProcessorEditor::applyAdvancedVisibility()
{
    const float opacity=advancedProgress*advancedProgress*(3-2*advancedProgress);
    for(auto* component:std::initializer_list<juce::Component*>{&mode,&modeLabel,&lookahead,&lookaheadLabel,&attack,&attackLabel,&release,&releaseLabel,&transientLink,&transientLinkLabel,&releaseLink,&releaseLinkLabel,&linkGroupLabel,&instant}){
        float alpha=opacity;
        if(component==&lookahead||component==&lookaheadLabel)alpha*=lookahead.isEnabled()?1.f:.35f;
        if(component==&attack||component==&attackLabel)alpha*=attack.isEnabled()?1.f:.35f;
        if(component==&instant)alpha*=instantOpacity;
        component->setAlpha(alpha);component->setVisible(alpha>0);
        component->getProperties().set("badgeObstacleOpacity",component==&instant?opacity*instantOpacity:opacity);
    }
    for(auto* control:std::initializer_list<juce::Component*>{&mode,&lookahead,&attack,&release,&transientLink,&releaseLink})control->setInterceptsMouseClicks(advancedShown&&opacity>.05f,advancedShown&&opacity>.05f);
    instant.setInterceptsMouseClicks(advancedShown&&opacity>.05f&&controlStyle==8,advancedShown&&opacity>.05f&&controlStyle==8);
    display.repaint();
}
void OpenLimiterAudioProcessorEditor::setHelp(bool enabled)
{
    if(enabled&&!tooltips)tooltips=std::make_unique<juce::TooltipWindow>(this,700);else if(!enabled)tooltips.reset();
    p.editorState.help=enabled;p.editorStateChanged();
}
void OpenLimiterAudioProcessorEditor::stopListening()
{p.stopMonitoring();delta.setToggleState(false,juce::dontSendNotification);compare.setToggleState(false,juce::dontSendNotification);}
void OpenLimiterAudioProcessorEditor::setDisplayActive(bool enabled)
{
    if(enabled==displayActive)return;displayActive=enabled;display.setActive(enabled);
    vblank={};pacer.reset();meters.resetAnimation();lastNative=lastFrame=0;lastNativeStamp=-1;
    if(enabled)
    {
        // Restore a shared current reading before the first native paint.
        if(getPeer()!=nullptr)
        {meters.tick(juce::Time::getMillisecondCounterHiRes());for(int i=0;i<3;++i)display.setLoudnessTail(i,meters.displayLoudness(i));}
        vblank=juce::VBlankAttachment(this,[this](double timestamp)
        {
            const double arrival=juce::Time::getMillisecondCounterHiRes(),native=timestamp*1000;
            if(lastNativeStamp<0||native<=lastNativeStamp||arrival-lastNative>250)
                nativeOffset=arrival-native;
            lastNative=arrival;lastNativeStamp=native;
            if(pacer.present(native))animate(juce::jmax(lastFrame,native+nativeOffset));
        });startTimerHz(60);
    }
    else{lnf.dismissPopupMenus();drive.cancelEditing();if(ready)startTimerHz(2);}
}
void OpenLimiterAudioProcessorEditor::visibilityChanged(){if(ready)setDisplayActive(isVisible());}
void OpenLimiterAudioProcessorEditor::parentHierarchyChanged(){if(ready&&getPeer())setDisplayActive(isShowing());}
void OpenLimiterAudioProcessorEditor::updateStyleMenu()
{
    const int quality=p.outputQuality();const double rate=p.processingRate();
    if(menuQuality==quality&&juce::exactlyEqual(menuRate,rate))return;
    menuQuality=quality;menuRate=rate;
    std::array<int,9> order{2,7,6,4,1,3,0,5,8};
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return open_limiter::pathLatency(rate,a,quality,false)>open_limiter::pathLatency(rate,b,quality,false);});
    const auto names=LimiterStyles::names();
    mode.clear(juce::dontSendNotification);for(int id:order)mode.addItem(names[(size_t)id],id+1);
    mode.setSelectedId(juce::roundToInt(p.parameters.getRawParameterValue("styleSelect")->load())+1,juce::dontSendNotification);
}
void OpenLimiterAudioProcessorEditor::updateStyleControls(int style)
{
    const bool live=p.zeroLatencyRequested();if(controlStyle==style&&controlInstant==live)return;controlStyle=style;controlInstant=live;
    const bool safe=style==4,zero=style==8;
    instant.setEnabled(zero);instant.setInterceptsMouseClicks(zero,zero);
    truePeak.setEnabled(!live);outputQuality.setEnabled(!live);renderQuality.setEnabled(!live);
    truePeak.setToggleState(!live&&p.parameters.getRawParameterValue("truePeak")->load()>.5f,juce::dontSendNotification);
    outputQuality.setSelectedId(live?1:juce::roundToInt(p.parameters.getRawParameterValue("outputQuality")->load())+1,juce::dontSendNotification);
    renderQuality.setText(live?"Render: Off":renderQuality.getItemText(juce::roundToInt(p.parameters.getRawParameterValue("renderQuality")->load())),juce::dontSendNotification);
    lookahead.setEnabled(!safe&&!zero);attack.setEnabled(!safe);
    lookahead.setTooltip(safe?"Guard uses its own anticipation timing; Lookahead has no effect in this style.":zero?"Zero uses fixed 0.35 ms anticipation; Instant removes it. Lookahead has no effect in either variant.":"Transient preview time, 0 to 5 ms, within the style’s existing audio delay. Double-click restores 0.180 ms.");
    attack.setTooltip(safe?"Guard uses its own attack timing; Attack has no effect in this style.":"Attack of the slower release stage. Fast values engage sustained reduction sooner; slow values leave more work to the short transient stage. The ceiling stays protected. Values accept ms or s.");
    applyAdvancedVisibility();
}
void OpenLimiterAudioProcessorEditor::animate(double now)
{
    updateStyleMenu();updateStyleControls(juce::roundToInt(p.parameters.getRawParameterValue("styleSelect")->load()));
    const float target=controlStyle==8?1.f:0.f;const float step=float(juce::jlimit(0.,120.,lastFrame>0?now-lastFrame:120.)/120.);
    const float before=instantOpacity;instantOpacity=instantOpacity<target?juce::jmin(target,instantOpacity+step):juce::jmax(target,instantOpacity-step);
    const float advancedBefore=advancedProgress,advancedStep=float(juce::jlimit(0.,220.,lastFrame>0?now-lastFrame:0.)/220.);
    advancedProgress=advancedShown?juce::jmin(1.f,advancedProgress+advancedStep):juce::jmax(0.f,advancedProgress-advancedStep);
    if(before!=instantOpacity||advancedBefore!=advancedProgress)applyAdvancedVisibility();
    if(!displayActive)return;lastFrame=now;meters.tick(now);for(int i=0;i<3;++i)display.setLoudnessTail(i,meters.displayLoudness(i));display.tick(now);
    updateReadouts();
    undo.setEnabled(p.undoManager.canUndo());redo.setEnabled(p.undoManager.canRedo());
    slotA.setToggleState(p.getSnapshot()==0,juce::dontSendNotification);slotB.setToggleState(p.getSnapshot()==1,juce::dontSendNotification);
    const auto progress=p.comparisonProgress();
    compare.setButtonText(p.monitoring()!=2?juce::String("Compare"):progress>=.999f?"Match "+juce::String(p.comparisonDb(),1)+" dB":"Match "+juce::String(juce::roundToInt(progress*100))+"%");
    delta.setToggleState(p.monitoring()==1,juce::dontSendNotification);
    pause.setToggleState(p.isLoudnessPaused(),juce::dontSendNotification);
    compare.setToggleState(p.monitoring()==2,juce::dontSendNotification);updateBarMotion(now);
}
void OpenLimiterAudioProcessorEditor::updateReadouts()
{
    const bool live=p.zeroLatencyRequested();
    if(live){truePeak.setToggleState(false,juce::dontSendNotification);outputQuality.setSelectedId(1,juce::dontSendNotification);renderQuality.setText("Render: Off",juce::dontSendNotification);}
    const bool limiting=p.parameters.getRawParameterValue("truePeak")->load()>.5f&&!live;
    truePeakMeter.setToggleState(p.parameters.getRawParameterValue("truePeakMeter")->load()>.5f,juce::dontSendNotification);
    const auto presetTitle=p.currentPresetName()+(p.presetModified()?" *":"");if(presetButton.getButtonText()!=presetTitle)presetButton.setButtonText(presetTitle);
    latencyLabel.setText(juce::String(p.getLatencySamples())+" samples",juce::dontSendNotification);
    latencyLabel.setTooltip(juce::String(1000.0*p.getLatencySamples()/juce::jmax(1.0,p.processingRate()),3)+" ms of actual lookahead, oversampling and protection delay.");
    const auto suffix=limiting?" dBTP":" dBFS";
    if(ceiling.getTextValueSuffix()!=suffix){ceiling.setTextValueSuffix(suffix);ceiling.updateText();}
}
void OpenLimiterAudioProcessorEditor::timerCallback()
{
    if(getPeer())setDisplayActive(isShowing());if(!displayActive)return;
    if(p.editorState.revision!=viewRevision)restoreView();
    const auto now=juce::Time::getMillisecondCounterHiRes();if(now-lastNative>100)animate(now);
}
bool OpenLimiterAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if(key==juce::KeyPress::escapeKey){stopListening();drive.cancelEditing();freeze.setToggleState(false,juce::dontSendNotification);display.setFrozen(false);return true;}
    if(key.getModifiers().isCommandDown()&&(key.getKeyCode()=='Z'||key.getKeyCode()=='z'))
    {stopListening();if(key.getModifiers().isShiftDown())p.undoManager.redo();else p.undoManager.undo();return true;}
    return false;
}
void OpenLimiterAudioProcessorEditor::showResetOptions()
{
    juce::PopupMenu menu;menu.setLookAndFeel(&lnf);menu.addItem(1,"Reset on playback",true,p.autoResetEnabled());
    juce::Component::SafePointer<OpenLimiterAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&reset),[safe](int result)
    {if(safe&&result==1){safe->p.setAutoReset(!safe->p.autoResetEnabled());safe->p.editorStateChanged();}});
}
void OpenLimiterAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(LT::bg);g.setColour(LT::grid.withAlpha(.5f));g.drawHorizontalLine(39,12.f,float(getWidth())-12.f);
    for(auto* control:std::initializer_list<juce::Component*>{&undo,&slotA,&freeze,&advanced,&bypass})g.drawVerticalLine(control->getX()-2,11.f,31.f);
    g.setColour(LT::text);g.setFont(juce::FontOptions(20.f,juce::Font::bold));g.drawText("OPEN LIMITER",18,7,190,25,juce::Justification::left);
    g.setColour(LT::gold);g.fillRoundedRectangle(18,35,52,2.5f,1.25f);
}

void OpenLimiterAudioProcessorEditor::resized()
{
    const int w=getWidth(),h=getHeight(),rightPadding=20,railWidth=juce::jlimit(264,300,juce::roundToInt((w-rightPadding)/3.4)),rail=w-rightPadding-railWidth;
    const int scaleGutter=juce::roundToInt(50.f*railWidth/270.f),plotHeight=h-150;
    display.setBounds(0,40,rail+scaleGutter,h-66);display.setChartHeight(plotHeight);
    meters.setBounds(rail+scaleGutter,40,railWidth-scaleGutter+rightPadding,h-66);meters.setLayout(plotHeight,scaleGutter,rightPadding);
    const auto floating=[&](juce::Component& control,int x,int y,int width,int height){control.setBounds(x-display.getX(),y-display.getY(),width,height);};
    int right=w-14;const auto toolbar=[&](juce::Component& c,int width){right-=width;c.setBounds(right,7,width,28);right-=4;};
    toolbar(bypass,76);toolbar(advanced,28);toolbar(help,24);toolbar(freeze,62);toolbar(copy,44);toolbar(slotB,24);toolbar(slotA,24);toolbar(redo,24);toolbar(undo,24);
    presetPrevious.setBounds(208,7,20,28);presetNext.setBounds(right-24,7,20,28);presetButton.setBounds(232,7,juce::jmax(60,right-260),28);
    driveLabel.setVisible(false);floating(drive,19,68,76,h-225);
    floating(mode,96,h-184,175,25);floating(instant,279,h-184,76,25);
    // Match the visible box edge, including the label glyph shadow.
    modeLabel.setBounds(mode.getX()+1,h-201-display.getY(),mode.getWidth()-1,13);
    const int knobsX=14,knobsY=h-144,controlsWidth=juce::jmin(462,rail-28),gap=5;
    const int linkingWidth=juce::jmax(104,juce::roundToInt(controlsWidth*.23f));
    const int knobWidth=(controlsWidth-linkingWidth-4*gap)/4;
    const auto place=[&](int x,juce::Slider& knob,juce::Label& label)
    {floating(label,x,knobsY,knobWidth,16);floating(knob,x,knobsY+16,knobWidth,90);knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,knobWidth,18);};
    place(knobsX,ceiling,ceilingLabel);place(knobsX+knobWidth+gap,lookahead,lookaheadLabel);
    place(knobsX+2*(knobWidth+gap),attack,attackLabel);place(knobsX+3*(knobWidth+gap),release,releaseLabel);
    const int linkingX=knobsX+4*(knobWidth+gap),linkWidth=(linkingWidth-4)/2;
    floating(linkGroupLabel,linkingX,knobsY,linkingWidth,16);
    const auto small=[&](int x,juce::Slider& knob,juce::Label& label)
    {floating(label,x,knobsY+22,linkWidth,12);floating(knob,x,knobsY+34,linkWidth,72);knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,linkWidth,18);};
    small(linkingX,transientLink,transientLinkLabel);small(linkingX+linkWidth+4,releaseLink,releaseLinkLabel);
    panel={0,h-26,w,26};detailBackdrop.setBounds(panel);detailBackdrop.setVisible(true);
    const bool compact=w<840;
    const int rowY=h-24;int x=10;const auto footer=[&](juce::Component& c,int width){c.setBounds(x,rowY,width,22);x+=width+(compact?5:7);};
    footer(outputQuality,compact?76:86);footer(renderQuality,compact?112:114);footer(truePeak,compact?60:72);
    x-=(compact?5:7)-2;footer(truePeakMeter,compact?48:58);
    footer(delta,compact?44:56);footer(compare,compact?66:90);footer(historyView,compact?62:76);footer(trace,compact?60:70);
    latencyLabel.setBounds(w-115,rowY,88,22);
    const int loudBottom=plotHeight-28+12,footerWidth=136;
    const int loudControls=meters.getY()+loudBottom+64;
    const int controlsLeft=rail+juce::roundToInt(200.f*railWidth/270.f)-footerWidth/2;
    pause.setBounds(controlsLeft,loudControls,20,23);loudnessView.setBounds(controlsLeft+25,loudControls,footerWidth-50,23);reset.setBounds(controlsLeft+footerWidth-20,loudControls,20,23);
    if(ready){p.editorState.width=w;p.editorState.height=h;p.editorState.details=true;}repaint();
    if(resizableCorner)resizableCorner->setBounds(w-17,h-17,14,14);
}

#include "PresetUI.h"

juce::Colour LimiterLookAndFeel::valueAccent(juce::Component& c)
{ if(auto* slider=c.findParentComponentOfClass<juce::Slider>();slider&&slider->getName()=="CEILING")return LT::red;
    return LT::gold; }
void LimiterLookAndFeel::fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))valueStyle.observeEditor(editor);
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::fillEditor(g,w,h,LT::bg);
    else juce::LookAndFeel_V4::fillTextEditorBackground(g,w,h,editor);
}
void LimiterLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int w,int h,juce::TextEditor& editor)
{
    if(OpenEditableValueStyle::isValueEditor(editor))OpenEditableValueStyle::outlineEditor(g,w,h,editor,valueAccent(editor),LT::grid);
    else juce::LookAndFeel_V4::drawTextEditorOutline(g,w,h,editor);
}
