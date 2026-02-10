#include "MenuBar.hpp"
#include <iostream>

namespace GreedyTangle {

MenuBar::~MenuBar() {
  if (font) {
    TTF_CloseFont(font);
    font = nullptr;
  }
}

bool MenuBar::Init(SDL_Renderer *rend, const std::string &fontPath) {
  renderer = rend;

  if (TTF_Init() == -1) {
    std::cerr << "[MenuBar] TTF_Init failed: " << TTF_GetError() << std::endl;
    return false;
  }

  // Try multiple font paths - comprehensive search
  std::vector<std::string> fontPaths = {
      fontPath,
      // Windows Fonts
      "c:/windows/fonts/arial.ttf",
      "c:/windows/fonts/consola.ttf",
      "c:/windows/fonts/segoeui.ttf",
      // macOS Fonts
      "/Library/Fonts/Arial.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      // Linux/Unix Fonts
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed.ttf",
      // DejaVu fonts (other distros)
      // Noto fonts (common on modern Linux)
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/opentype/noto/NotoSans-Regular.otf",
      "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto-fonts/NotoSans-Regular.ttf",
      // Ubuntu fonts
      "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
      "/usr/share/fonts/ubuntu/Ubuntu-R.ttf",
      // Liberation fonts
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
      // Free fonts
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/usr/share/fonts/freefont/FreeSans.ttf",
      // Cantarell (GNOME default)
      "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
      "/usr/share/fonts/abattis-cantarell/Cantarell-Regular.otf",
      "/usr/share/fonts/opentype/cantarell/Cantarell-Regular.otf",
      // Droid fonts
      "/usr/share/fonts/truetype/droid/DroidSans.ttf",
      "/usr/share/fonts/droid/DroidSans.ttf",
      // Roboto
      "/usr/share/fonts/truetype/roboto/Roboto-Regular.ttf",
      "/usr/share/fonts/google-roboto/Roboto-Regular.ttf",
      // Hack font (programmer font)
      "/usr/share/fonts/truetype/hack/Hack-Regular.ttf",
      // Generic fallback locations
      "/usr/share/fonts/truetype/ttf-bitstream-vera/Vera.ttf",
      "/usr/share/fonts/TTF/Vera.ttf"};

  for (const auto &path : fontPaths) {
    if (path.empty())
      continue;
    font = TTF_OpenFont(path.c_str(), 13);
    if (font) {
      std::cout << "[MenuBar] Loaded font: " << path << std::endl;
      break;
    }
  }

  if (!font) {
    std::cerr << "[MenuBar] Could not load any font!" << std::endl;
    return false;
  }

  return true;
}

void MenuBar::AddMenu(const std::string &title,
                      const std::vector<MenuItem> &items) {
  Menu menu;
  menu.title = title;
  menu.items = items;
  menu.isOpen = false;
  menus.push_back(menu);
  RecalculateLayout();
}

void MenuBar::RecalculateLayout() {
  int x = PADDING;

  for (Menu &menu : menus) {
    int titleWidth = GetTextWidth(menu.title) + PADDING * 2;
    menu.titleRect = {x, 0, titleWidth, BAR_HEIGHT};

    // Calculate dropdown height
    int dropdownHeight = 8; // Top/bottom padding
    for (const MenuItem &item : menu.items) {
      dropdownHeight += item.isSeparator ? 9 : ITEM_HEIGHT;
    }

    menu.dropdownRect = {x, BAR_HEIGHT, DROPDOWN_WIDTH, dropdownHeight};
    x += titleWidth;
  }
}

int MenuBar::GetTextWidth(const std::string &text) {
  if (!font)
    return static_cast<int>(text.length() * 8);
  int w, h;
  TTF_SizeText(font, text.c_str(), &w, &h);
  return w;
}

bool MenuBar::HandleEvent(const SDL_Event &event) {
  if (event.type == SDL_MOUSEMOTION) {
    int mx = event.motion.x;
    int my = event.motion.y;

    hoveredMenuIndex = -1;
    hoveredItemIndex = -1;

    // Check menu titles
    for (size_t i = 0; i < menus.size(); ++i) {
      SDL_Point pt = {mx, my};
      if (SDL_PointInRect(&pt, &menus[i].titleRect)) {
        hoveredMenuIndex = static_cast<int>(i);
        if (anyMenuOpen) {
          // Switch to hovered menu if any menu is open
          for (Menu &m : menus)
            m.isOpen = false;
          menus[i].isOpen = true;
        }
        break;
      }
    }

    // Check dropdown items if menu is open
    for (size_t i = 0; i < menus.size(); ++i) {
      if (menus[i].isOpen) {
        SDL_Point pt = {mx, my};
        if (SDL_PointInRect(&pt, &menus[i].dropdownRect)) {
          int y = menus[i].dropdownRect.y + 4;
          for (size_t j = 0; j < menus[i].items.size(); ++j) {
            int itemH = menus[i].items[j].isSeparator ? 9 : ITEM_HEIGHT;
            if (my >= y && my < y + itemH && !menus[i].items[j].isSeparator) {
              hoveredItemIndex = static_cast<int>(j);
              hoveredMenuIndex = static_cast<int>(i);
              break;
            }
            y += itemH;
          }
        }
        break;
      }
    }

    return my < BAR_HEIGHT || anyMenuOpen;
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;

    // Check menu title clicks
    for (size_t i = 0; i < menus.size(); ++i) {
      SDL_Point pt = {mx, my};
      if (SDL_PointInRect(&pt, &menus[i].titleRect)) {
        menus[i].isOpen = !menus[i].isOpen;
        anyMenuOpen = menus[i].isOpen;
        // Close other menus
        for (size_t j = 0; j < menus.size(); ++j) {
          if (j != i)
            menus[j].isOpen = false;
        }
        return true;
      }
    }

    // Check dropdown item clicks
    for (size_t i = 0; i < menus.size(); ++i) {
      if (menus[i].isOpen) {
        SDL_Point pt = {mx, my};
        if (SDL_PointInRect(&pt, &menus[i].dropdownRect)) {
          int y = menus[i].dropdownRect.y + 4;
          for (size_t j = 0; j < menus[i].items.size(); ++j) {
            MenuItem &item = menus[i].items[j];
            int itemH = item.isSeparator ? 9 : ITEM_HEIGHT;
            if (my >= y && my < y + itemH && !item.isSeparator) {
              if (item.action) {
                item.action();
              }
              if (item.isCheckable) {
                item.isChecked = true;
              }
              CloseAllMenus();
              return true;
            }
            y += itemH;
          }
        } else {
          // Clicked outside dropdown, close it
          CloseAllMenus();
          return my < BAR_HEIGHT;
        }
      }
    }

    // Click in bar area but not on menu
    if (my < BAR_HEIGHT) {
      CloseAllMenus();
      return true;
    }
  }

  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
    if (anyMenuOpen) {
      CloseAllMenus();
      return true;
    }
  }

  return false;
}

void MenuBar::CloseAllMenus() {
  for (Menu &menu : menus) {
    menu.isOpen = false;
  }
  anyMenuOpen = false;
  hoveredMenuIndex = -1;
  hoveredItemIndex = -1;
}

void MenuBar::Render() {
  // Draw bar background
  SDL_SetRenderDrawColor(renderer, Colors::BAR_BG.r, Colors::BAR_BG.g,
                         Colors::BAR_BG.b, Colors::BAR_BG.a);

  int windowWidth;
  SDL_GetRendererOutputSize(renderer, &windowWidth, nullptr);
  SDL_Rect barRect = {0, 0, windowWidth, BAR_HEIGHT};
  SDL_RenderFillRect(renderer, &barRect);

  // Draw bottom border
  SDL_SetRenderDrawColor(renderer, Colors::DROPDOWN_BORDER.r,
                         Colors::DROPDOWN_BORDER.g, Colors::DROPDOWN_BORDER.b,
                         255);
  SDL_RenderDrawLine(renderer, 0, BAR_HEIGHT - 1, windowWidth, BAR_HEIGHT - 1);

  // Draw menu titles
  for (size_t i = 0; i < menus.size(); ++i) {
    const Menu &menu = menus[i];
    bool isHovered =
        (static_cast<int>(i) == hoveredMenuIndex && hoveredItemIndex == -1);
    bool isActive = menu.isOpen;

    // Highlight background
    if (isHovered || isActive) {
      SDL_SetRenderDrawColor(renderer, Colors::ITEM_HOVER.r,
                             Colors::ITEM_HOVER.g, Colors::ITEM_HOVER.b,
                             Colors::ITEM_HOVER.a);
      SDL_RenderFillRect(renderer, &menu.titleRect);
    }

    // Render text
    if (font) {
      SDL_Surface *surface =
          TTF_RenderText_Blended(font, menu.title.c_str(), Colors::TEXT);
      if (surface) {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
          int textX = menu.titleRect.x + PADDING;
          int textY = (BAR_HEIGHT - surface->h) / 2;
          SDL_Rect dst = {textX, textY, surface->w, surface->h};
          SDL_RenderCopy(renderer, texture, nullptr, &dst);
          SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
      }
    }
  }

  // Draw open dropdowns
  for (const Menu &menu : menus) {
    if (menu.isOpen) {
      RenderDropdown(menu);
    }
  }
}

void MenuBar::RenderDropdown(const Menu &menu) {
  // Draw dropdown background
  SDL_SetRenderDrawColor(renderer, Colors::ITEM_BG.r, Colors::ITEM_BG.g,
                         Colors::ITEM_BG.b, Colors::ITEM_BG.a);
  SDL_RenderFillRect(renderer, &menu.dropdownRect);

  // Draw border
  SDL_SetRenderDrawColor(renderer, Colors::DROPDOWN_BORDER.r,
                         Colors::DROPDOWN_BORDER.g, Colors::DROPDOWN_BORDER.b,
                         255);
  SDL_RenderDrawRect(renderer, &menu.dropdownRect);

  // Draw items
  int y = menu.dropdownRect.y + 4;
  for (size_t i = 0; i < menu.items.size(); ++i) {
    const MenuItem &item = menu.items[i];

    if (item.isSeparator) {
      // Draw separator line
      SDL_SetRenderDrawColor(renderer, Colors::SEPARATOR.r, Colors::SEPARATOR.g,
                             Colors::SEPARATOR.b, Colors::SEPARATOR.a);
      int lineY = y + 4;
      SDL_RenderDrawLine(renderer, menu.dropdownRect.x + 8, lineY,
                         menu.dropdownRect.x + menu.dropdownRect.w - 8, lineY);
      y += 9;
      continue;
    }

    // Item background (hover)
    SDL_Rect itemRect = {menu.dropdownRect.x + 2, y, menu.dropdownRect.w - 4,
                         ITEM_HEIGHT};

    if (static_cast<int>(i) == hoveredItemIndex) {
      SDL_SetRenderDrawColor(renderer, Colors::ITEM_HOVER.r,
                             Colors::ITEM_HOVER.g, Colors::ITEM_HOVER.b,
                             Colors::ITEM_HOVER.a);
      SDL_RenderFillRect(renderer, &itemRect);
    }

    // Checkmark
    if (item.isCheckable && item.isChecked) {
      if (font) {
        SDL_Surface *checkSurf =
            TTF_RenderText_Blended(font, "✓", Colors::CHECKMARK);
        if (checkSurf) {
          SDL_Texture *checkTex =
              SDL_CreateTextureFromSurface(renderer, checkSurf);
          if (checkTex) {
            SDL_Rect checkDst = {menu.dropdownRect.x + 8, y + 3, checkSurf->w,
                                 checkSurf->h};
            SDL_RenderCopy(renderer, checkTex, nullptr, &checkDst);
            SDL_DestroyTexture(checkTex);
          }
          SDL_FreeSurface(checkSurf);
        }
      }
    }

    // Item text
    if (font) {
      SDL_Surface *surface =
          TTF_RenderText_Blended(font, item.text.c_str(), Colors::TEXT);
      if (surface) {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
          int textX = menu.dropdownRect.x + (item.isCheckable ? 28 : 12);
          int textY = y + (ITEM_HEIGHT - surface->h) / 2;
          SDL_Rect dst = {textX, textY, surface->w, surface->h};
          SDL_RenderCopy(renderer, texture, nullptr, &dst);
          SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
      }
    }

    y += ITEM_HEIGHT;
  }
}

void MenuBar::SetItemChecked(int menuIndex, int itemIndex, bool checked) {
  if (menuIndex >= 0 && menuIndex < static_cast<int>(menus.size())) {
    Menu &menu = menus[menuIndex];
    if (itemIndex >= 0 && itemIndex < static_cast<int>(menu.items.size())) {
      menu.items[itemIndex].isChecked = checked;
    }
  }
}

void MenuBar::RenderText(const std::string &text, int x, int y,
                         SDL_Color color) {
  if (!font || !renderer)
    return;

  SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), color);
  if (surface) {
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
      SDL_Rect dst = {x, y, surface->w, surface->h};
      SDL_RenderCopy(renderer, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
  }
}

void MenuBar::RenderTextCentered(const std::string &text, SDL_Rect rect,
                                 SDL_Color color) {
  if (!font || !renderer)
    return;

  SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), color);
  if (surface) {
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
      int x = rect.x + (rect.w - surface->w) / 2;
      int y = rect.y + (rect.h - surface->h) / 2;
      SDL_Rect dst = {x, y, surface->w, surface->h};
      SDL_RenderCopy(renderer, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
  }
}

} // namespace GreedyTangle
