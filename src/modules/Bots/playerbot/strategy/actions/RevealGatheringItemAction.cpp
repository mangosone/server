#include "botpch.h"
#include "../../playerbot.h"
#include "RevealGatheringItemAction.h"

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

using namespace ai;
using namespace MaNGOS;

uint64 extractGuid(WorldPacket& packet);

/**
 * @brief Checks for any game object within a specified range of a given object
 */
class AnyGameObjectInObjectRangeCheck
{
public:
    AnyGameObjectInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
    WorldObject const& GetFocusObject() const { return *i_obj; }
    bool operator()(GameObject* u)
    {
        if (u && i_obj->IsWithinDistInMap(u, i_range) && u->isSpawned() && u->GetGOInfo())
        {
            return true;
        }

        return false;
    }

private:
    WorldObject const* i_obj; /**< The object to check around */
    float i_range; /**< The range to check within */
};

/**
 * @brief Executes the action to reveal gathering items
 *
 * @param event The event that triggered the action
 * @return true if the action was executed successfully, false otherwise
 */
bool RevealGatheringItemAction::Execute(Event event)
{
    if (!bot->GetGroup())
    {
        return false;
    }

    list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, sPlayerbotAIConfig.grindDistance);
    GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects((const WorldObject*)bot, searcher, sPlayerbotAIConfig.reactDistance);

    vector<GameObject*> result;
    for(list<GameObject*>::iterator tIter = targets.begin(); tIter != targets.end(); ++tIter)
    {
        GameObject* go = *tIter;
        if (!go || !go->isSpawned() || bot->GetDistance2d(go) <= sPlayerbotAIConfig.lootDistance)
        {
            continue;
        }

        if (LockEntry const *lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId()))
        {
            for (int i = 0; i < 8; ++i)
            {
                if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                {
                    uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                    uint32 reqSkillValue = max((uint32)2, lockInfo->Skill[i]);
                    if ((skillId == SKILL_MINING || skillId == SKILL_HERBALISM) &&
                            ai->HasSkill((SkillType)skillId) && uint32(bot->GetPureSkillValue(skillId)) >= reqSkillValue)
                    {
                        result.push_back(go);
                        break;
                    }
                }
            }
        }

        if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE && ai->HasSkill(SKILL_FISHING))
        {
            result.push_back(go);
        }
    }

    if (result.empty())
    {
        return false;
    }

    GameObject *go = result[urand(0, result.size() - 1)];
    if (!go) return false;

    ostringstream msg;
    msg << "I see a " << ChatHelper::formatGameobject(go) << ". ";
    switch (go->GetGoType())
    {
      case GAMEOBJECT_TYPE_CHEST:
          msg << "Let's look at it.";
          break;
      case GAMEOBJECT_TYPE_FISHINGNODE:
          msg << "Let's fish a bit.";
          break;
      default:
          msg << "Should we go nearer?";
    }

    // everything is fine, do it
    WorldPacket data(MSG_MINIMAP_PING, (8 + 4 + 4));
    data << bot->GetObjectGuid();
    data << go->GetPositionX();
    data << go->GetPositionY();
    bot->GetGroup()->BroadcastPacket(&data, true, -1, bot->GetObjectGuid());
    bot->Say(msg.str(), LANG_UNIVERSAL);

    return true; // Ensure that the method returns a value in all control paths
}