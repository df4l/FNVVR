#pragma once

namespace dxvk {

  /**
   * \brief What the game is showing, as far as the VR layer cares
   */
  enum class VrGameStateKind {
    Unknown,
    MainMenu,
    Loading,
    /// A menu other than the Pip-Boy pauses the game
    Menu,
    PipBoy,
    InGame,
  };

  /**
   * \brief Reads the game's main menu, loading screen and menu mode flags
   *
   * Only reads memory and calls one side-effect-free game function, on the
   * game's main thread. The addresses and how they were found are in
   * findings/main-loop.md.
   */
  class VrGameState {

  public:

    /**
     * \brief Checks that the executable is the supported version
     * \returns \c false if the state cannot be read, read() then reports Unknown
     */
    bool initialize();

    /**
     * \brief Reads the current state
     */
    VrGameStateKind read() const;

    static const char* name(VrGameStateKind kind);

  private:

    bool m_available = false;

  };

}
