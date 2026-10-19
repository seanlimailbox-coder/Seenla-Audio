#pragma once
#include <JuceHeader.h>

// One short-lived UI animation clock per look-and-feel. Observing component
// events preserves each plugin's existing slider and text-entry gestures.
class OpenEditableValueStyle final : private juce::Timer,
                                     public juce::MouseListener,
                                     public juce::KeyListener,
                                     private juce::ComponentListener
{
public:
    ~OpenEditableValueStyle() override
    {
        stopTimer();
        for (auto& owner : owners) if (owner != nullptr)
        { owner->removeMouseListener(this); owner->removeKeyListener(this); }
        for(auto& component:observed)if(component!=nullptr)component->removeComponentListener(this);
    }

    void paintLabel(juce::Graphics& g, juce::Label& label,
                    juce::Colour background, juce::Colour accent)
    {
        if (!label.isEditable()) return;
        auto* slider=label.findParentComponentOfClass<juce::Slider>();
        auto& entry=watch(label,slider);
        if (label.isBeingEdited() || !usable(entry)) return;
        const bool cell=label.getComponentID().startsWith("list_");
        paintField(g,label.getLocalBounds().toFloat(),entry,background,accent,
                   cell ? .32f : 0.f,cell ? .18f : .65f);
    }

    // The limiter's floating gain value is painted by its slider rather than
    // a Label. Restrict hover to the value badge, not the full vertical track.
    void paintValue(juce::Graphics& g, juce::Slider& slider,
                    juce::Rectangle<float> area,juce::Colour background,juce::Colour accent)
    {
        auto& entry=watch(slider,&slider);entry.region=area;entry.customRegion=true;
        const bool over=slider.isMouseOver(true)&&area.contains(slider.getMouseXYRelative().toFloat());
        if(entry.over!=over){entry.over=over;wake();}
        if (usable(entry)) paintField(g,area,entry,background,accent);
    }

    static bool isValueEditor(juce::TextEditor& editor)
    {
        if (editor.findParentComponentOfClass<juce::Slider>() != nullptr) return true;
        if (auto* label=editor.findParentComponentOfClass<juce::Label>()) return label->isEditable();
        return false;
    }
    void observeEditor(juce::TextEditor& editor)
    {
        if(editor.getJustificationType()!=juce::Justification::centred)editor.setJustification(juce::Justification::centred);
        if(editor.getLeftIndent()!=0||editor.getTopIndent()!=0)editor.setIndents(0,0);
        observe(editor);
    }
    static void fillEditor(juce::Graphics& g,int w,int h,juce::Colour background)
    { g.setColour(background);g.fillRoundedRectangle(.5f,.5f,w-1.f,h-1.f,3.f); }
    static void outlineEditor(juce::Graphics& g,int w,int h,juce::TextEditor& editor,
                              juce::Colour accent,juce::Colour grid)
    {
        g.setColour((editor.hasKeyboardFocus(true)?accent:grid).withMultipliedAlpha(editor.isEnabled()?.8f:.3f));
        g.drawRoundedRectangle(.5f,.5f,w-1.f,h-1.f,3.f,1.f);
    }

    void mouseEnter(const juce::MouseEvent& e) override { hoverEvent(e,false); }
    void mouseMove(const juce::MouseEvent& e) override { hoverEvent(e,false); }
    void mouseExit(const juce::MouseEvent& e) override { hoverEvent(e,true); }
    void mouseDown(const juce::MouseEvent& e) override
    { if(e.mods.isLeftButtonDown()) forEvent(e,[](Entry& x){x.pressed=true;}); }
    void mouseUp(const juce::MouseEvent& e) override
    { forEvent(e,[](Entry& x){x.pressed=false;}); }
    void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails&) override
    { const auto until=juce::Time::getMillisecondCounterHiRes()+100;forEvent(e,[until](Entry& x){x.pulseUntil=until;}); }
    bool keyPressed(const juce::KeyPress& key,juce::Component* source) override
    {
        const auto code=key.getKeyCode();
        if(code==juce::KeyPress::upKey||code==juce::KeyPress::downKey
           ||code==juce::KeyPress::leftKey||code==juce::KeyPress::rightKey
           ||code==juce::KeyPress::homeKey||code==juce::KeyPress::endKey)
        {
            const auto until=juce::Time::getMillisecondCounterHiRes()+100;
            for(auto& x:entries)if(matches(x,source))x.pulseUntil=until;
            wake();
        }
        return false;
    }

private:
    struct Entry
    {
        juce::Component::SafePointer<juce::Component> target,owner;
        juce::Rectangle<float> region;
        float hover=0,line=0;
        bool over=false,pressed=false,customRegion=false;
        double pulseUntil=0;
    };
    std::vector<Entry> entries;
    std::vector<juce::Component::SafePointer<juce::Component>> owners;
    std::vector<juce::Component::SafePointer<juce::Component>> observed;
    double lastTime=0;

    Entry& watch(juce::Component& target,juce::Slider* slider)
    {
        for(auto& entry:entries)if(entry.target==&target)return entry;
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const Entry& x){return x.target==nullptr;}),entries.end());
        auto* owner=slider?static_cast<juce::Component*>(slider):&target;
        bool listening=false;for(auto& existing:owners)listening|=existing==owner;
        if(!listening){owners.emplace_back(owner);owner->addMouseListener(this,true);owner->addKeyListener(this);}
        entries.emplace_back();auto& entry=entries.back();entry.target=&target;entry.owner=owner;
        observe(target);observe(*owner);
        entry.over=target.isMouseOver(true);if(entry.over)wake();
        return entry;
    }
    void observe(juce::Component& component)
    {
        for(auto& existing:observed)if(existing==&component)return;
        observed.erase(std::remove_if(observed.begin(),observed.end(),[](const auto& x){return x==nullptr;}),observed.end());
        observed.emplace_back(&component);component.addComponentListener(this);
    }
    void componentChildrenChanged(juce::Component&) override { wake(); }
    void componentVisibilityChanged(juce::Component& component) override { availabilityChanged(component); }
    void componentEnablementChanged(juce::Component& component) override { availabilityChanged(component); }
    void componentBeingDeleted(juce::Component& component) override
    {
        for(auto& x:entries){if(x.target==&component)x.target=nullptr;if(x.owner==&component)x.owner=nullptr;}
    }
    void availabilityChanged(juce::Component& component)
    {
        if(!component.isEnabled()||!component.isVisible())
            for(auto& x:entries)if(x.target==&component||x.owner==&component){x.over=x.pressed=false;x.pulseUntil=0;}
        wake();
    }
    static bool usable(const Entry& x)
    { return x.target!=nullptr&&x.owner!=nullptr&&x.target->isEnabled()&&x.owner->isEnabled()&&x.target->isVisible()&&x.owner->isVisible(); }
    static bool matches(const Entry& x,juce::Component* source)
    { return source!=nullptr&&x.owner!=nullptr&&(x.owner==source||x.owner->isParentOf(source)); }
    static bool editing(juce::Component& component)
    {
        if(auto* label=dynamic_cast<juce::Label*>(&component))return label->isBeingEdited();
        for(auto* child:component.getChildren())
            if(auto* editor=dynamic_cast<juce::TextEditor*>(child);editor&&editor->isVisible())return true;
        return false;
    }
    template<class Action> void forEvent(const juce::MouseEvent& e,Action action)
    {
        bool changed=false;
        for(auto& x:entries)if(matches(x,e.originalComponent)){action(x);changed=true;}
        if(changed)wake();
    }
    void hoverEvent(const juce::MouseEvent& e,bool leaving)
    {
        bool changed=false;
        for(auto& x:entries)if(x.target!=nullptr&&matches(x,e.originalComponent))
        {
            const auto area=x.customRegion?x.region:x.target->getLocalBounds().toFloat();
            const bool over=!leaving&&area.contains(e.getEventRelativeTo(x.target).position);
            if(x.over!=over){x.over=over;changed=true;}
        }
        if(changed)wake();
    }
    void wake()
    { if(!isTimerRunning()){lastTime=juce::Time::getMillisecondCounterHiRes();startTimerHz(60);} }
    static float approach(float before,float target,float seconds)
    {
        const float step=seconds/(target>before?.10f:.18f);
        return before<target?juce::jmin(target,before+step):juce::jmax(target,before-step);
    }
    void timerCallback() override
    {
        const auto now=juce::Time::getMillisecondCounterHiRes();
        const float elapsed=float(juce::jlimit(0.,.05,(now-lastTime)*.001));lastTime=now;
        bool moving=false;
        for(auto& x:entries)if(x.target!=nullptr)
        {
            const bool enabled=usable(x);
            if(!enabled){x.over=x.pressed=false;x.pulseUntil=0;}
            const bool pulse=now<x.pulseUntil;
            const float hover=enabled&&x.over?1.f:0.f;
            const float line=enabled?(x.pressed||pulse||editing(*x.target)?.8f:x.over?.45f:0.f):0.f;
            const float h=approach(x.hover,hover,elapsed),l=approach(x.line,line,elapsed);
            moving|=h!=hover||l!=line||pulse;
            if(h!=x.hover||l!=x.line)
            {
                x.hover=h;x.line=l;
                x.target->getProperties().set("openValueHover",h);
                x.target->getProperties().set("openValueLine",l);
                x.target->repaint();
            }
        }
        if(!moving)stopTimer();
    }
    static void paintField(juce::Graphics& g,juce::Rectangle<float> area,const Entry& x,
                           juce::Colour background,juce::Colour accent,float base=0,float hover=.65f)
    {
        g.setColour(background.withAlpha(base+x.hover*hover));g.fillRoundedRectangle(area.reduced(1),3.f);
        if(x.line>0){const auto a=area.reduced(4,1);g.setColour(accent.withAlpha(x.line));g.drawLine(a.getX(),a.getBottom(),a.getRight(),a.getBottom(),1.f);}
    }
};
