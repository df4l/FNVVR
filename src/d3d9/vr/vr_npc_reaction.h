#pragma once

#include <cstdint>
#include <memory>

#include "vr_npc_temper.h"

namespace dxvk {

  /**
   * \brief Makes an actor react to being grabbed by the player
   *
   * The game has no reaction of its own to an actor grabbed by the hand.
   * The closest is its comment on a weapon aimed at an actor
   * (ProcessLists::CommentOnPlayerAction, findings/grab.md §19.3): the
   * actor says a line of the topic the game keeps for it ("Watch where
   * you're pointing that thing"), under the same conditions and the same
   * timer shared by every comment on the player's actions. A foolhardy,
   * aggressive actor with no recorded line for that topic goes into combat
   * with the player instead, as with the script command StartCombat, unless
   * one of its factions idolizes the player (vrNpcGrabResponse,
   * findings/grab.md §19.8).
   *
   * Only used on the game's main thread.
   */
  class VrNpcReaction {

  public:

    ~VrNpcReaction();

    /**
     * \brief Finds the game's functions
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrNpcReaction> install();

    /**
     * \brief An actor was just grabbed by a hand
     *
     * \param [in] actor The living actor (Actor*)
     */
    void grabbed(uint8_t* actor);

    /**
     * \brief Whether a hand keeps an actor from walking away
     *
     * \param [in] actor The living actor (Actor*)
     * \param [in] player The player (PlayerCharacter*)
     * \returns \c true if the player is the stronger (vrNpcHeldInPlace)
     */
    bool heldInPlace(uint8_t* actor, uint8_t* player) const;

  private:

    VrNpcReaction() = default;

    uint32_t m_loggedReactions   = 0;
    uint32_t m_loggedAudioMisses = 0;

    VrNpcTemper readTemper(uint8_t* actor);

    bool hasVoicedLine(uint8_t* actor);

    bool isVoiced(uint8_t* actor, void* topic, uint8_t* info);

    bool factionIdolizesPlayer(uint8_t* actor) const;

    void complain(uint8_t* actor);

    void attack(uint8_t* actor);

  };

}
