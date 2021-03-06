//
// Created by desktop on 11.08.16.
//

#include "DialogManager.h"
#include <engine/World.h>
#include <utils/logger.h>
#include <ui/DialogBox.h>
#include <components/VobClasses.h>
#include <engine/BaseEngine.h>
#include <logic/messages/EventManager.h>
#include <logic/PlayerController.h>
#include <ui/SubtitleBox.h>
#include <ui/PrintScreenMessages.h>

/**
 * File containing the dialouges
 * // TODO: Be able to read this from VDF
 */
const char* OU_FILE = "/_work/DATA/scripts/content/CUTSCENE/OU.BIN"; // German and international versions use different extensions (bin/dat)
const char* OU_FILE_2 = "/_work/DATA/scripts/content/CUTSCENE/OU.DAT";

using namespace Logic;



DialogManager::DialogManager(World::WorldInstance& world) :
    m_World(world)
{
    m_ActiveDialogBox = nullptr;
    m_ScriptDialogMananger = nullptr;
    m_ActiveSubtitleBox = nullptr;
    m_DialogActive = false;
}

DialogManager::~DialogManager()
{
    delete m_ScriptDialogMananger;

    endDialog();

    m_World.getEngine()->getRootUIView().removeChild(m_ActiveSubtitleBox);
    delete m_ActiveSubtitleBox;
    m_ActiveSubtitleBox = nullptr;
}

void DialogManager::onAIProcessInfos(Daedalus::GameState::NpcHandle self,
                                     std::vector<Daedalus::GameState::InfoHandle> infos)
{
    // Set information about the current interaction
    m_Interaction.target = self;
    m_Interaction.infos = infos;

    // Get interaction partner ("other")
    m_Interaction.player = ZMemory::handleCast<Daedalus::GameState::NpcHandle>(
            getVM().getDATFile().getSymbolByName("other").instanceDataHandle);

    if(infos.empty())
        return;


    LogInfo() << "Started talking with: " << getGameState().getNpc(m_Interaction.target).name[0];
    LogInfo() << "Options: ";

    clearChoices();

    // Acquire all information we should be able to see right now
    for(size_t i=0;i<infos.size();i++)
    {
        Daedalus::GEngineClasses::C_Info& info = getVM().getGameState().getInfo(infos[i]);

        // Test if we should be able to see this info
        int32_t valid = 0;
        if(info.condition)
        {
            m_World.getScriptEngine().prepareRunFunction();
            valid = m_World.getScriptEngine().runFunctionBySymIndex(info.condition);
        }

        if(valid)
        {
            if(info.description.empty() && info.important)
                info.description = "<important>";

            ChoiceEntry entry;
            entry.nr = info.nr;
            entry.functionSym = info.information;
            entry.text = info.description;
            entry.info = infos[i];

            addChoice(entry);
        }
    }

    flushChoices();

    // Do the important choice right now
    // TODO: Let the NPC begin talking in that case
   /* if(importantChoice != static_cast<size_t>(-1))
    {
        if(!performChoice(importantChoice))
        {
            // END was chosen, don't continue the dialog
            endDialog();
        }
    }*/
}

void DialogManager::onAIOutput(Daedalus::GameState::NpcHandle self, Daedalus::GameState::NpcHandle target,
                               const ZenLoad::oCMsgConversationData& msg)
{
    m_ActiveSubtitleBox->setText(getGameState().getNpc(self).name[0], msg.text);

    // Make a new message for the talking NPC
    VobTypes::NpcVobInformation selfnpc = VobTypes::getVobFromScriptHandle(m_World, self);


    /*if(!targetnpc.playerController)
    {
        LogWarn() << "AI_Output: Target not found/invalid!";
        return;
    }*/

    if(!selfnpc.playerController)
    {
        LogWarn() << "AI_Output: Self not found/invalid!";
        return;
    }

    if(target.isValid())
        LogInfo() << "AIOutput: From " << getGameState().getNpc(self).name[0] << " to " << getGameState().getNpc(target).name[0];
    else
        LogInfo() << "AIOutput: From " << getGameState().getNpc(self).name[0] << " (no target)";

    EventMessages::ConversationMessage conv;
    conv.subType = EventMessages::ConversationMessage::ST_Output;
    conv.name = msg.name;
    conv.text = msg.text;

    VobTypes::NpcVobInformation targetnpc = VobTypes::getVobFromScriptHandle(m_World, target);

    if(targetnpc.isValid())
    {
        conv.target = targetnpc.entity;

        // Check if the target is currently talking to us
        EventMessages::EventMessage* otherconv = targetnpc.playerController->getEM().getTalkingWithMessage(
                selfnpc.entity);

        // Wait for the other npc to complete first
        if (otherconv)
            selfnpc.playerController->getEM().waitForMessage(otherconv);
    }

    // Push the actual conversation-message
    selfnpc.playerController->getEM().onMessage(conv);
}


void DialogManager::update(double dt)
{
    if(m_ActiveDialogBox)
    {
        static bool visibilityHack = m_ActiveSubtitleBox->isHidden();
        m_ActiveDialogBox->setHidden(!(m_ActiveSubtitleBox->isHidden() && visibilityHack));
        m_DialogActive = !m_ActiveSubtitleBox->isHidden() || !m_ActiveDialogBox->isHidden();

        visibilityHack = m_ActiveSubtitleBox->isHidden();

        if (m_ActiveDialogBox->getChoiceTaken() != -1)
        {
            // Perform the choice and check if that was an END-choice
            if (!performChoice(static_cast<size_t>(m_ActiveDialogBox->getChoiceTaken())))
            {
                // END was chosen, don't continue the dialog
                endDialog();

                // Clear the dialog partners EMs
                // FIXME: I dont think the original game does this, but NPCs won't change their state after talking
                //        sometimes (baar parvez for example)
                VobTypes::NpcVobInformation playerVob = VobTypes::getVobFromScriptHandle(m_World, m_Interaction.player);
                VobTypes::NpcVobInformation targetVob = VobTypes::getVobFromScriptHandle(m_World, m_Interaction.target);

                // Start routine
                EventMessages::StateMessage msg;
                msg.subType = EventMessages::StateMessage::EV_StartState;
                msg.functionSymbol = 0;

                if(playerVob.isValid())
                    playerVob.playerController->getEM().onMessage(msg, playerVob.entity);

                if(targetVob.isValid())
                    targetVob.playerController->getEM().onMessage(msg, playerVob.entity);
            } else
            {
                // There is more! Start talking again.
                flushChoices();
                //endDialog();
                //startDialog(m_Interaction.target);
            }
        }
    }

    m_DialogActive = !(!m_ActiveDialogBox && m_ActiveSubtitleBox->isHidden());
}

Daedalus::DaedalusVM& DialogManager::getVM()
{
    return m_World.getScriptEngine().getVM();
}

Daedalus::GameState::DaedalusGameState& DialogManager::getGameState()
{
    return m_World.getScriptEngine().getGameState();
}

bool DialogManager::performChoice(size_t choice)
{
    assert(choice < m_Interaction.choices.size());

    // Hide the options box
    endDialog();

    // Get actual selected info-object
    Daedalus::GEngineClasses::C_Info& info = getGameState().getInfo(m_Interaction.choices[choice].info);

    // Set instances again, since they could have been changed across the frames
    getVM().setInstance("self", ZMemory::toBigHandle(m_Interaction.target), Daedalus::IC_Npc);
    getVM().setInstance("other", ZMemory::toBigHandle(m_Interaction.player), Daedalus::IC_Npc);

    size_t fnSym = m_Interaction.choices[choice].functionSym;

    clearChoices();

    // Call the script routine attached to the choice
    m_World.getScriptEngine().prepareRunFunction();
    m_World.getScriptEngine().runFunctionBySymIndex(fnSym);

    // We now know this information. Do this before actually triggering the dialog, since then we can update the
    // choices right away. This is important because scripts may overwrite these again!
    m_ScriptDialogMananger->setNpcInfoKnown(getGameState().getNpc(m_Interaction.player).instanceSymbol, info.instanceSymbol);

    if(m_Interaction.choices.empty())
    {
        // We chose "back" or haven't gotten to a sub-dialog
        updateChoices();
    }

    return info.nr != 999;

}

void DialogManager::startDialog(Daedalus::GameState::NpcHandle target)
{
    if(m_DialogActive)
        return;

    Handle::EntityHandle playerEntity = m_World.getScriptEngine().getPlayerEntity();
    VobTypes::NpcVobInformation playerVob = VobTypes::asNpcVob(m_World, playerEntity);
    VobTypes::NpcVobInformation targetVob = VobTypes::asNpcVob(m_World, VobTypes::getEntityFromScriptInstance(m_World, target));

    LogInfo() << "Talk to: " << VobTypes::getScriptObject(targetVob).name[0];

    // Self is the NPC we're talking to here. Kind of reversed, but what do I know.
    getVM().setInstance("self", ZMemory::toBigHandle(target), Daedalus::IC_Npc);
    getVM().setInstance("other", ZMemory::toBigHandle(VobTypes::getScriptHandle(playerVob)), Daedalus::IC_Npc);

    targetVob.playerController->standUp();

    m_DialogActive = true;

    EventMessages::StateMessage msg;
    msg.subType = EventMessages::StateMessage::EV_StartState;
    msg.functionSymbol = m_World.getScriptEngine().getSymbolIndexByName("ZS_TALK");

    // Set other/victum // TODO: Refractor
    msg.other = ZMemory::handleCast<Daedalus::GameState::NpcHandle>(m_World.getScriptEngine().getVM().getDATFile().getSymbolByName("OTHER").instanceDataHandle);
    msg.victim = ZMemory::handleCast<Daedalus::GameState::NpcHandle>(m_World.getScriptEngine().getVM().getDATFile().getSymbolByName("VICTIM").instanceDataHandle);

    targetVob.playerController->getEM().onMessage(msg, playerVob.entity);

    /*m_World.getScriptEngine().prepareRunFunction();
    m_World.getScriptEngine().runFunction(getVM().getDATFile().getSymbolByName("ZS_Talk").address);*/
}

void DialogManager::endDialog()
{
    m_World.getEngine()->getRootUIView().removeChild(m_ActiveDialogBox);

    delete m_ActiveDialogBox;
    m_ActiveDialogBox = nullptr;

    m_DialogActive = false;
}

void DialogManager::init()
{
    std::string ou = Utils::getCaseSensitivePath(OU_FILE, m_World.getEngine()->getEngineArgs().gameBaseDirectory);

    if(ou.empty())
        ou = Utils::getCaseSensitivePath(OU_FILE_2, m_World.getEngine()->getEngineArgs().gameBaseDirectory);

    if(ou.empty())
        LogWarn() << "Failed to read OU-file!";

    m_ScriptDialogMananger = new Daedalus::GameState::DaedalusDialogManager(getVM(), ou);

    // Register externals
    auto onAIOutput = [&](Daedalus::GameState::NpcHandle self, Daedalus::GameState::NpcHandle target, const ZenLoad::oCMsgConversationData& msg)
    {
        LogInfo() << getGameState().getNpc(self).name[0] << ": " << msg.text;
        DialogManager::onAIOutput(self, target, msg);
    };

    auto onAIProcessInfos = [&](Daedalus::GameState::NpcHandle self, std::vector<Daedalus::GameState::InfoHandle> infos)
    {
        DialogManager::onAIProcessInfos(self, infos);
    };

    m_ScriptDialogMananger->registerExternals(onAIOutput, onAIProcessInfos);

    // Add subtitle box (Hidden if there is nothing to display)
    m_ActiveSubtitleBox = new UI::SubtitleBox();
    m_World.getEngine()->getRootUIView().addChild(m_ActiveSubtitleBox);
    m_ActiveSubtitleBox->setHidden(true);

    m_PrintScreenMessageView = new UI::PrintScreenMessages();
}

void DialogManager::displaySubtitle(const std::string& subtitle, const std::string& self)
{
    m_ActiveSubtitleBox->setHidden(false);
    m_ActiveSubtitleBox->setText(self, subtitle);
}

void DialogManager::stopDisplaySubtitle()
{
    m_ActiveSubtitleBox->setHidden(true);
}

void DialogManager::clearChoices()
{
    m_Interaction.choices.clear();

    flushChoices();
}

size_t DialogManager::addChoice(ChoiceEntry& entry)
{
    m_Interaction.choices.push_back(entry);

    if(m_Interaction.choices.back().nr == -2)
        m_Interaction.choices.back().nr = static_cast<int>(m_Interaction.choices.size()) - 1;

    flushChoices();

	return 0;
}

void DialogManager::sortChoices()
{
    std::sort(m_Interaction.choices.begin(), m_Interaction.choices.end(), [](const ChoiceEntry& a, const ChoiceEntry& b){
        return a.nr < b.nr;
    });
}

void DialogManager::flushChoices()
{
    // Sort by importance index
    sortChoices();

    if(m_ActiveDialogBox)
        endDialog();

    // Open dialog box
    m_ActiveDialogBox = new UI::DialogBox();
    for(ChoiceEntry& e : m_Interaction.choices)
        m_ActiveDialogBox->addChoice(e);

    m_World.getEngine()->getRootUIView().addChild(m_ActiveDialogBox);
}

void DialogManager::updateChoices()
{
    m_ScriptDialogMananger->processInfosFor(m_Interaction.target);
}

