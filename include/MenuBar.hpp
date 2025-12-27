#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <functional>
#include <string>
#include <vector>

namespace GreedyTangle {

/**
 * MenuItem - Individual menu item with optional checkbox
 */
struct MenuItem {
  std::string text;
  std::function<void()> action;
  bool isCheckable = false;
  bool isChecked = false;
  bool isSeparator = false; // Horizontal line separator

  MenuItem() : isSeparator(true) {} // Separator constructor

  MenuItem(const std::string &txt, std::function<void()> act,
           bool checkable = false, bool checked = false)
      : text(txt), action(act), isCheckable(checkable), isChecked(checked),
        isSeparator(false) {}
};

/**
 * Menu - Dropdown menu containing items
 */
struct Menu {
  std::string title;
  std::vector<MenuItem> items;
  bool isOpen = false;
  SDL_Rect titleRect;    // Position of menu title in bar
  SDL_Rect dropdownRect; // Position of dropdown panel
};

/**
 * MenuBar - VS Code-style menu bar with dropdowns
 */
class MenuBar {
public:
  // Visual constants
  static constexpr int BAR_HEIGHT = 28;
  static constexpr int ITEM_HEIGHT = 24;
  static constexpr int PADDING = 12;
  static constexpr int DROPDOWN_WIDTH = 180;

  // Colors (VS Code dark theme inspired)
  struct Colors {
    static constexpr SDL_Color BAR_BG = {37, 37, 38, 255};
    static constexpr SDL_Color ITEM_BG = {37, 37, 38, 255};
    static constexpr SDL_Color ITEM_HOVER = {62, 62, 64, 255};
    static constexpr SDL_Color TEXT = {204, 204, 204, 255};
    static constexpr SDL_Color TEXT_DIM = {128, 128, 128, 255};
    static constexpr SDL_Color SEPARATOR = {72, 72, 74, 255};
    static constexpr SDL_Color CHECKMARK = {75, 175, 100, 255};
    static constexpr SDL_Color DROPDOWN_BORDER = {69, 69, 69, 255};
  };

private:
  SDL_Renderer *renderer = nullptr;
  TTF_Font *font = nullptr;
  std::vector<Menu> menus;
  int hoveredMenuIndex = -1;
  int hoveredItemIndex = -1;
  bool anyMenuOpen = false;

public:
  MenuBar() = default;
  ~MenuBar();

  /**
   * Initialize with renderer and load font
   * @return true on success
   */
  bool Init(SDL_Renderer *rend, const std::string &fontPath = "");

  /**
   * Add a menu to the bar
   */
  void AddMenu(const std::string &title, const std::vector<MenuItem> &items);

  /**
   * Handle mouse events
   * @return true if event was consumed by menu
   */
  bool HandleEvent(const SDL_Event &event);

  /**
   * Render the menu bar and any open dropdowns
   */
  void Render();

  /**
   * Check if any menu is currently open
   */
  bool IsMenuOpen() const { return anyMenuOpen; }

  /**
   * Close all open menus
   */
  void CloseAllMenus();

  /**
   * Get bar height for layout calculations
   */
  int GetHeight() const { return BAR_HEIGHT; }

  /**
   * Update a checkable menu item's state
   */
  void SetItemChecked(int menuIndex, int itemIndex, bool checked);

  /**
   * Render text at a specific position (for external use)
   */
  void RenderText(const std::string &text, int x, int y, SDL_Color color);

  /**
   * Render text centered in a rectangle
   */
  void RenderTextCentered(const std::string &text, SDL_Rect rect,
                          SDL_Color color);

private:
  void RecalculateLayout();
  void RenderDropdown(const Menu &menu);
  int GetTextWidth(const std::string &text);
};

} // namespace GreedyTangle
