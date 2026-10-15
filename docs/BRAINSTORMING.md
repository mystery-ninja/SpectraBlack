<!-- 
    This is human written and may not be edited by AI.
    AI can take content from this file and edit the AI generated documentations.
    Assume that this file is the most recent documentation regards brainstorming and planing.
--> 

# App

Spectra Black is a new app combining Avee RE Player (originally made by Daaw Aww, modded by Cubiq) and Adobe After Effects. The idea is to combine all 
useful features from both apps and make a new App out of it.
Since the After Effect visualizer community isnt that big but use a complete app and the Avee Player community being massive but its app has lots of 
lacking features, the vision for this new app is to make both sides happy, combining 2 communities into one big one. It will not only allow users to 
make even fancier visualizer with lots of extras but also allow phone users who dont have a pc or laptop to also use this app and create art. In modern 
time and age, art is dying and the goal with this app is to bring back beauty, inspiration and remove greed, jealousy and the concept of gatekeeping.


# Contributors (current)

- MysterY   - Project leader
- Zion      - Kind helper
- Ayashii   - Kind helper 
- Schxma    - Kind helper 
... more coming soon, maybe.


# Effects

- AudioSpectrum (from After Effects, a combination of AudioProvider and Bars in Avee RE Player - Bars in After Effects look better but combining with setting-ideas from Avee RE Player, example: Smooth which exists in Avee but not in AE. Also AE renders audio correct. Avee does render reverbed bass as extreme bass which is false and a bug)
- SoundKeys (from After Effects, a combination of AudioProvider (app takes the last AudioProvider in the layers as reaction factor) in Avee RE Player - Soundkeys are better, taking that over)
- Text
- Particles
- Ray Effects
- Blur Effect
- Zoom Blur Effect
- Colorama/ColorCorrection
- DummyElement (own customizable element with glsl)
- Liquify/Turbulant Displacements
- FOV Effect
- Pixelation
- EdgeEffect
(every effect that exists in Avee and AE - those i currently have)
... more coming soon, maybe.


# Features

- Keyframes (Like After Effects)
- Project file explorer (Like After Effects)
- Elements/Importable Plugins (Like AE and Avee)
- Collaborations (new)
- Fresh UI (new, 95% from AE but with some slight changes. Thinking of a good mobile gui so using the app is comfortable that combines full viz view but also editor accessibility)
- 3D/2D scenes (3DBox from Avee, 3D Scenes from AE, prefering AE style cuz more controlled)
- Layers (Like AE)
- Preview (Life or full render)
- Render
- JavaScript-like expression language (like AE)
- Multiframe rendering (Like AE)
- Life Render (Like AE)


# GUI

To keep it short, basically like AE, but with some changes, like:
- Render button instead of `Composition > Add to render queue` and then rendering
- Player progress bar with pause, skip 5seconds back and forth, next or previous songs.


# File extensions
- spec: Visualizer/Project file (like .aep in After Effects and .viz in Avee Player - .aep and .viz should be compatible with Spectra Black)
- sbx:  Plugin/Effect file that can be imported natively in Spectra Black. Lets be nice and provide a template for those who want to code their own sbx plugins. Language? Javascript/Typescript) Plugin needs to be zip that contains folder with the name of the plugin and the plugin files


# Uncategorized/Uncertancies

Avee has kind of a cool concept with Pos/Scale/Rot which i try to bring into this AE Design but AE has plugins which can modify shake, scale and all that and therefore looking for a design that is compatible is still open.

A slowing down previews feature that on rendering (live or frame-by-frame) you can change the playback speed for close observations.

I like the idea for masking in Avee Player more than in AE cuz in avee you select an image or composition and on the "Mask Image" you can simply pick a shape, either from another composition or something else - In AE understandings, it would be layers. You will select a layer within the composition to pick a shape for a different layer. Kind of a fucked up explanation but i tried lol)

Thinking about adding a song library system just like in Avee player. The Project file stuff is still like AE but without the song files.
