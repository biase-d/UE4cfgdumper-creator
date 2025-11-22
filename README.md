# UE4 Dumper & Creator

An enhanced, interactive tool for the Nintendo Switch that allows you to scan running Unreal Engine 4 & 5 games, manage dumps, and **create custom cheat presets directly on your console**

## Key Features

*   **Applet Mode (Auto-Scan):** Instantly scan a running game to find engine settings and generate a default cheat file
*   **Manage Mode (Full UI):** A robust TUI (Text User Interface) to browse your game library
*   **Preset Editor (Experimental):** Create complex cheat toggles (like "60 FPS", "Resolution Scale", "Disable Bloom") without a PC
    *   Supports **Hex**, **Float**, and **Decimal** input
    *   Automatically maps game-specific addresses from your scan logs
*   **Cheat Manager:**
    *   View installed cheat files
    *   Toggle cheats ON/OFF globally
    *   Batch generate cheats for your entire library at once

## How to Use

### 1. Scanning a Game (Applet Mode)
*Use this to find the settings for a new game*

1.  Launch your target UE4/UE5 game and reach the main menu
2.  Press the **HOME** button
3.  Open the **Album** to launch the Homebrew Menu (Applet Mode)
4.  Run `UE4cfgdumper+creator`
5.  The tool will auto-scan the game and generate a default cheat file

### 2. Managing & Creating Cheats (Title Mode)
*Use this to create custom presets and manage your library*

1.  Close any running games
2.  Hold the **R** button while launching a game to open the Homebrew Menu in **Title Mode** (Full RAM)
3.  Run `UE4cfgdumper+creator`

#### The Game Dashboard
Select a game from the list to open its Dashboard. From here you can:
*   **Toggle Status:** Enable or Disable the cheat file
*   **View File:** Read the raw cheat codes
*   **Update Cheats:** Re-generate the cheat file using the Default config or a Custom Preset
*   **Preset Editor:** Enter the advanced editor

### 3. Using the Preset Editor
The editor allows you to build a `preset.json` file on the Switch

1.  Select **Preset Editor (Advanced)** in the Dashboard
2.  Choose **[+ Create New Preset]**
3.  **Add Options:** Create toggleable cheats (e.g., "60 FPS")
4.  **Select Variables:** You will see a list of all variables found in the game. Check the ones you want to modify
5.  **Edit Values:**
    *   Press **(Y)** to edit a value
    *   Press **(X)** to toggle between **HEX**, **FLOAT**, and **DECIMAL** input modes
6.  **Save:** Press **(+)** to save your preset
7.  **Apply:** Go back to the Dashboard, select **Update Cheats (Select Preset)**, and choose your new file

## File Structure

*   **Dumps/Logs:** `/switch/UE4cfgdumper/`
*   **Presets:** `/config/ue4cheatcreator-proper/presets/`
*   **Generated Cheats:** `/atmosphere/contents/<TitleID>/cheats/`

## Credits

*   **Based on** [UE4cfgdumper](https://github.com/MasaGratoR/UE4cfgdumper) by **MasaGratoR**

---
*Disclaimer: Use at your own risk. Always back up your save files before using cheats*