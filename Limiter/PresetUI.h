#pragma once

void OpenLimiterAudioProcessorEditor::presetError(const juce::String& message)
{juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Open Limiter",message,"OK",this);}
void OpenLimiterAudioProcessorEditor::stepPreset(int direction)
{
    presetItems=LimiterPresets::list();if(presetItems.empty())return;int current=direction>0?-1:0;
    for(size_t i=0;i<presetItems.size();++i)if(presetItems[i].key==p.currentPresetKey()){current=int(i);break;}
    const int next=(current+direction+int(presetItems.size()))%int(presetItems.size());juce::String error;
    if(!p.loadPreset(presetItems[(size_t)next],error))presetError(error);updateReadouts();
}
void OpenLimiterAudioProcessorEditor::savePresetAs()
{
    auto name=juce::File::createLegalFileName(p.currentPresetName());if(name.isEmpty())name="My Preset";
    LimiterPresets::userFolder().createDirectory();
    presetChooser=std::make_unique<juce::FileChooser>("Save Open Limiter preset",LimiterPresets::userFolder().getChildFile(name+".openlimiter"),"*.openlimiter");
    juce::Component::SafePointer<OpenLimiterAudioProcessorEditor> safe(this);
    presetChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& chooser){if(!safe)return;auto file=chooser.getResult();if(file==juce::File{})return;juce::String error;
            if(!safe->p.savePreset(file.withFileExtension("openlimiter"),error))safe->presetError(error);safe->updateReadouts();});
}
void OpenLimiterAudioProcessorEditor::showPresets()
{
    presetItems=LimiterPresets::list();juce::PopupMenu menu;menu.setLookAndFeel(&lnf);juce::StringArray categories;
    for(const auto& preset:presetItems)categories.addIfNotAlreadyThere(preset.factory?preset.category:"User");
    for(const auto& category:categories){juce::PopupMenu group;
        for(size_t i=0;i<presetItems.size();++i){const auto& preset=presetItems[i];if((preset.factory?preset.category:juce::String("User"))==category)
            group.addItem(1000+int(i),preset.name,true,preset.key==p.currentPresetKey());}
        menu.addSubMenu(category,group);
    }
    const auto ellipsis=juce::String::charToString(0x2026);
    menu.addSeparator();menu.addItem(1,"Open preset"+ellipsis);menu.addItem(2,"Save",p.isUserPreset());menu.addItem(3,"Save As"+ellipsis);
    menu.addItem(4,"Rename"+ellipsis,p.isUserPreset());menu.addItem(5,"Delete"+ellipsis,p.isUserPreset());menu.addSeparator();menu.addItem(6,"Show user presets folder");menu.addItem(7,"Preset notes"+ellipsis,p.currentPresetNotes().isNotEmpty());
    juce::Component::SafePointer<OpenLimiterAudioProcessorEditor> safe(this);
    // Capture this menu's list so another editor/filesystem refresh cannot
    // redirect an already-open row to a different preset.
    // Match the style dropdown's row height so JUCE does not shrink the shared
    // popup font to fit its much shorter automatic preset rows.
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton).withStandardItemHeight(mode.getHeight()),[safe,items=presetItems](int result){
        if(!safe||result==0)return;juce::String error;
        if(result>=1000&&result<1000+int(items.size())){if(!safe->p.loadPreset(items[(size_t)(result-1000)],error))safe->presetError(error);}
        else if(result==1){safe->presetChooser=std::make_unique<juce::FileChooser>("Open Limiter preset",LimiterPresets::userFolder(),"*.openlimiter");
            safe->presetChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& chooser){if(!safe)return;const auto file=chooser.getResult();if(file==juce::File{})return;juce::String failure;if(!safe->p.loadPreset(file,failure))safe->presetError(failure);safe->updateReadouts();});}
        else if(result==2){if(!safe->p.savePreset(juce::File(safe->p.currentPresetKey()),error))safe->presetError(error);}
        else if(result==3)safe->savePresetAs();
        else if(result==4){const auto originalKey=safe->p.currentPresetKey();auto* dialog=new juce::AlertWindow("Rename preset","Choose a name for this user preset.",juce::MessageBoxIconType::NoIcon,safe.getComponent());
            dialog->addTextEditor("name",safe->p.currentPresetName(),"Name");dialog->addButton("Rename",1,juce::KeyPress(juce::KeyPress::returnKey));dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
            juce::Component::SafePointer<juce::AlertWindow> alert(dialog);dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,alert,originalKey](int answer){if(safe&&alert&&answer==1){if(safe->p.currentPresetKey()!=originalKey){safe->presetError("The selected preset changed. Choose Rename again.");return;}juce::String failure;if(!safe->p.renamePreset(alert->getTextEditorContents("name"),failure))safe->presetError(failure);safe->updateReadouts();}}),true);}
        else if(result==5){const auto originalKey=safe->p.currentPresetKey();juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,"Delete preset","Delete \""+safe->p.currentPresetName()+"\" from your preset library? The current sound stays loaded.","Delete","Cancel",safe.getComponent(),juce::ModalCallbackFunction::create([safe,originalKey](int answer){if(safe&&answer==1){if(safe->p.currentPresetKey()!=originalKey){safe->presetError("The selected preset changed. Choose Delete again.");return;}juce::String failure;if(!safe->p.deletePreset(failure))safe->presetError(failure);safe->updateReadouts();}}));}
        else if(result==6){auto folder=LimiterPresets::userFolder();folder.createDirectory();folder.revealToUser();}
        else if(result==7)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,safe->p.currentPresetName(),safe->p.currentPresetNotes(),"OK",safe.getComponent());
        safe->updateReadouts();
    });
}
void OpenLimiterAudioProcessorEditor::updateBarMotion(double now)
{
    const float step=(float)juce::jlimit(0.,120.,lastBarMotion<0?120.:now-lastBarMotion)/120.f;lastBarMotion=now;
    for(auto* c:std::initializer_list<juce::Component*>{&instant,&outputQuality,&renderQuality,&truePeak,&truePeakMeter,&delta,&compare,&historyView,&trace,&loudnessView,&pause,&reset,&undo,&redo,&help,&freeze,&slotA,&slotB,&copy,&bypass,&advanced,&presetPrevious,&presetButton,&presetNext}){
        const float hover=c->isMouseOver(true)?1.f:0.f;const auto ease=[&](const char* id,float target){const float before=(float)c->getProperties().getWithDefault(id,target);
            const float after=before<target?juce::jmin(target,before+step):juce::jmax(target,before-step);
            if(!c->getProperties().contains(id)||before!=after){c->getProperties().set(id,after);c->repaint();}};
        ease("barHover",hover);ease("barEnabled",c->isEnabled()?1.f:.35f);
        if(auto* button=dynamic_cast<juce::Button*>(c)){ease("barLit",button->getToggleState()?1.f:0.f);ease("barPress",button->isDown()?1.f:0.f);}
    }
}
