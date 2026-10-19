#pragma once
#include <JuceHeader.h>

// Visual-only popup transitions. Menus keep their original geometry and JUCE
// input handling; outgoing images never become modal or accept native input.
// The paired opt-in hook in juce_PopupMenu.cpp runs before item destruction.
class OpenPopupMenuMotion
{
    struct Ghost final : juce::Component
    {
        Ghost(juce::Component& menu,float opacity)
            : desktopScale(menu.getDesktopScaleFactor()),initialAlpha(opacity),started(juce::Time::getMillisecondCounterHiRes())
        {
            const auto* display=juce::Desktop::getInstance().getDisplays().getDisplayForRect(menu.getScreenBounds());
            const float scale=(display?(float)display->scale:1.f)*juce::Component::getApproximateScaleFactorForComponent(&menu);
            image=menu.createComponentSnapshot(menu.getLocalBounds(),true,scale);
            setName("Popup fade image");setAccessible(false);setWantsKeyboardFocus(false);
            setInterceptsMouseClicks(false,false);setAlwaysOnTop(true);setBounds(menu.getBounds());setTransform(menu.getTransform());setAlpha(opacity);
            if(auto* parent=menu.getParentComponent())parent->addAndMakeVisible(*this);
            else addToDesktop(juce::ComponentPeer::windowIsTemporary|juce::ComponentPeer::windowIgnoresMouseClicks|juce::ComponentPeer::windowIgnoresKeyPresses
                |(menu.getPeer()?(menu.getPeer()->getStyleFlags()&juce::ComponentPeer::windowHasDropShadow):0));
            setVisible(true);toFront(false);
        }
        float getDesktopScaleFactor()const override{return desktopScale;}
        void paint(juce::Graphics& g)override
        {g.drawImage(image,getLocalBounds().toFloat());}
        juce::Image image;
        float desktopScale,initialAlpha;
        double started;
    };
    struct State final : juce::Timer,juce::ComponentListener,std::enable_shared_from_this<State>
    {
        struct Window
        {
            juce::Component::SafePointer<juce::Component> component;
            juce::Component* identity;
            double started=0;
            float initialAlpha=0;
            bool opening=false,closing=false;
        };
        juce::Component::SafePointer<juce::Component> owner;
        std::vector<Window> windows;
        std::vector<std::unique_ptr<Ghost>> ghosts;
        ~State()override{clear();}
        static float ease(double progress)
        {const float p=(float)juce::jlimit(0.,1.,progress);return p*p*(3.f-2.f*p);}
        bool isHostActive()const
        {
            if(juce::Process::isForegroundProcess())return true;
            // Some AU hosts embed an editor from a helper process.
            const auto* peer=owner!=nullptr?owner->getPeer():nullptr;
            return peer!=nullptr&&peer->isFocused();
        }
        void prepare(juce::Component& menu)
        {
            if(!juce::Desktop::canUseSemiTransparentWindows())return;
            float from=0;const auto bounds=menu.getScreenBounds();
            // Reopening over a fading menu continues from its current opacity,
            // instead of stacking ghosts or flashing back to transparent.
            for(auto i=ghosts.begin();i!=ghosts.end();)
            {
                const auto old=(*i)->getScreenBounds(),overlap=old.getIntersection(bounds);
                if(!overlap.isEmpty())
                {
                    if((double)overlap.getWidth()*overlap.getHeight()>.6*juce::jmin((double)old.getWidth()*old.getHeight(),(double)bounds.getWidth()*bounds.getHeight()))
                        from=juce::jmax(from,(*i)->getAlpha());
                    i=ghosts.erase(i);
                }
                else ++i;
            }
            windows.push_back({&menu,&menu,0,from,false,false});menu.setAlpha(from);menu.addComponentListener(this);
            std::weak_ptr<State> weak=shared_from_this();juce::Component::SafePointer<juce::Component> safe(&menu);
            menu.getProperties().set("seenla.popupDismiss",juce::var(juce::var::NativeFunction([weak,safe](const juce::var::NativeFunctionArgs&)->juce::var
            {if(auto state=weak.lock())if(safe!=nullptr)state->close(*safe);return {};})));
            if(menu.isVisible())
            {windows.back().started=juce::Time::getMillisecondCounterHiRes();windows.back().opening=true;startTimerHz(60);}
        }
        void componentVisibilityChanged(juce::Component& menu)override
        {
            auto i=find(menu);if(i==windows.end()||i->closing)return;
            if(menu.isVisible())
            {i->started=juce::Time::getMillisecondCounterHiRes();i->opening=true;startTimerHz(60);}
        }
        void componentBeingDeleted(juce::Component& menu)override
        {windows.erase(std::remove_if(windows.begin(),windows.end(),[&](const Window& w){return w.identity==&menu;}),windows.end());}
        std::vector<Window>::iterator find(juce::Component& menu)
        {return std::find_if(windows.begin(),windows.end(),[&](const Window& w){return w.identity==&menu;});}
        void close(juce::Component& menu)
        {
            auto i=find(menu);if(i==windows.end()||i->closing)return;
            i->closing=true;i->opening=false;
            const float opacity=menu.getAlpha();
            if(opacity>.01f&&menu.isShowing()&&(owner==nullptr||owner->isShowing())&&isHostActive())
            {
                while(ghosts.size()>=4)ghosts.erase(ghosts.begin());
                ghosts.push_back(std::make_unique<Ghost>(menu,opacity));startTimerHz(60);
            }
            menu.setAlpha(0);
        }
        void timerCallback()override
        {
            if(owner!=nullptr&&!owner->isShowing()){clear();return;}
            if(!isHostActive())ghosts.clear();
            const double now=juce::Time::getMillisecondCounterHiRes();bool active=false;
            for(auto& window:windows)if(window.opening&&window.component!=nullptr)
            {
                const double duration=juce::jmax(30.,120.*(1-window.initialAlpha)),progress=(now-window.started)/duration;
                window.component->setAlpha(window.initialAlpha+(1-window.initialAlpha)*ease(progress));
                window.opening=progress<1;active|=window.opening;
            }
            for(auto i=ghosts.begin();i!=ghosts.end();)
            {
                const double duration=85.*juce::jmax(.4f,(*i)->initialAlpha),progress=(now-(*i)->started)/duration;
                if(progress>=1)i=ghosts.erase(i);
                else{(*i)->setAlpha((*i)->initialAlpha*(1-ease(progress)));active=true;++i;}
            }
            if(!active)stopTimer();
        }
        void clear()
        {
            stopTimer();ghosts.clear();auto previous=std::move(windows);windows.clear();
            for(auto& window:previous)if(window.component!=nullptr)
            {
                auto& menu=*window.component;menu.removeComponentListener(this);menu.getProperties().remove("seenla.popupDismiss");
                // A host may retain a popup until its asynchronous modal cleanup.
                // Detach our theme before its owning editor is destroyed.
                menu.setLookAndFeel(nullptr);menu.exitModalState(0);menu.setVisible(false);
            }
        }
    };
public:
    OpenPopupMenuMotion():state(std::make_shared<State>()){}
    ~OpenPopupMenuMotion(){state->clear();}
    void setOwner(juce::Component& owner){state->owner=&owner;}
    void prepare(juce::Component& menu){state->prepare(menu);}
    void dismiss(){state->clear();}
private:
    friend struct LimiterPopupMotionAudit;
    std::shared_ptr<State> state;
};
