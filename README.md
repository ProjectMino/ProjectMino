<p align="center">
  <img src="https://i.imgur.com/FenwBST.png" alt="Project Mino Logo">
</p>
<h1>⠀</h1>

# What is it?

Project Mino is a open sourse puzzle stacker simaller to tetris, developed by Mino Moves Inc. To improve the experience of competive players. Providing built in features for tournaments and more right out of the box!

# Build instructions.

> [!WARNING]  
> Project Mino is curently in heavy active development, and things always change. Building from sourse curently does not include auto-updating, meaning you'll have to build again to recieve new updates.

> [!NOTE]  
> These built instructions are meant primairlly created for Unix like systems, (MacOS/Linux.) Although you can use them on Windows using MSYS2, you should use Microsoft's own [Visual Studio](https://visualstudio.microsoft.com/) for the best experience!

<h1>⠀</h1>

## Prerequisites

apt (Debian/Ubuntu)
`
sudo apt update
sudo apt install build-essential libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev git
`

on macOS (homebrew)
`
brew install git sdl2 sdl2_ttf sdl2_image curl
`
and then install clang using
`
xcode-select --install
`

dnf (Fedora, RHEL, and OpenSUSE)
`
sudo dnf groupinstall "Development Tools"
sudo dnf install SDL2-devel SDL2_ttf-devel SDL2_image-devel libcurl-devel git
`

pacman (Arch Linux, and Manjaro)
`
sudo pacman -S base-devel sdl2 sdl2_ttf sdl2_image curl git
`

Then download the game's client and replay engine code with the commands below.
`
git clone https://github.com/ProjectMino/ProjectMino.git && cd ProjectMino && git clone https://github.com/ProjectMino/Pluviohiems.git
`
<h1>⠀</h1>

Now it's time to compile it!
Assuming your already in the game's directory and are using cmake, type the following commands in your terminal.
`
make clean
make
export 
./tetris
`

If you'd like the game to fetch background images from Unsplash, make sure to get your API key from, https://unsplash.com/developers then type the following command.
`
export UNSPLASH_ACCESS_KEY=YOUR_API_KEY_HERE
`

and the game client should start!

<h1>⠀</h1>

(C) Mino Moves Inc. 2025 - Present
