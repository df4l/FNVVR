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
     * \brief How far the player's grip exceeds the actor's Strength
     *
     * \param [in] actor The living actor (Actor*)
     * \param [in] player The player (PlayerCharacter*)
     * \param [in] hands How many hands hold the actor
     * \returns Above zero, the hands keep the actor (vrNpcGripMargin)
     */
    float gripMargin(uint8_t* actor, uint8_t* player, uint32_t hands) const;

    /**
     * \brief Whether an actor is in combat
     *
     * \param [in] actor The living actor (Actor*)
     */
    bool fighting(uint8_t* actor) const;

    /**
     * \brief Whether an actor is a companion of the player
     *
     * \param [in] actor The living actor (Actor*)
     */
    bool teammate(uint8_t* actor) const;

    /**
     * \brief Whether an actor takes being handled without getting angry
     *
     * A companion, or one of a faction that idolizes the player.
     *
     * \param [in] actor The living actor (Actor*)
     */
    bool forgives(uint8_t* actor) const;

    /**
     * \brief An actor's share of its Health, 0 to 1
     *
     * \param [in] actor The living actor (Actor*)
     */
    float healthShare(uint8_t* actor) const;

    /**
     * \brief An actor's base Health
     *
     * \param [in] actor The living actor (Actor*)
     */
    float maxHealth(uint8_t* actor) const;

    /**
     * \brief Makes a held actor complain again
     *
     * Under the game's own conditions and timer for comments.
     *
     * \param [in] actor The living actor (Actor*)
     */
    void nag(uint8_t* actor);

    /**
     * \brief An actor takes its handling for an assault
     *
     * As the script command SendAssaultAlarm on the actor: a crime others may
     * react to, and combat with the player.
     *
     * \param [in] actor The living actor (Actor*)
     */
    void assault(uint8_t* actor);

  private:

    VrNpcReaction() = default;

    uint32_t m_loggedReactions   = 0;
    uint32_t m_loggedAudioMisses = 0;
    bool     m_canAssault        = false;

    VrNpcTemper readTemper(uint8_t* actor);

    bool canComment(uint8_t* actor, bool teammate, bool inCombat) const;

    bool hasVoicedLine(uint8_t* actor);

    bool isVoiced(uint8_t* actor, void* topic, uint8_t* info);

    bool factionIdolizesPlayer(uint8_t* actor) const;

    void complain(uint8_t* actor);

    void attack(uint8_t* actor);

  };

}
