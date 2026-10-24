# Finding the render-distance code with Ghidra

Farion needs byte signatures from `libminecraftpe.so`, because Mojang's own function names are stripped from the file.
This guide gets you to the right functions on a PC (Windows, Mac or Linux). A phone cannot run Ghidra.

## 1. Install (one time)

1. **JDK 21 or newer**: https://adoptium.net (pick "Temurin 21", your OS, `.msi`/`.pkg`/`.tar.gz`).
2. **Ghidra**: https://github.com/NationalSecurityAgency/ghidra/releases - download the newest
   `ghidra_*_PUBLIC_*.zip` and unzip it anywhere. Run `ghidraRun.bat` (Windows) or `./ghidraRun` (Mac/Linux).
3. Your file: the `libminecraftpe.so` in the `libs` release of your Farion repo
   (https://github.com/kirisuniruu-commits/Farion/releases/tag/libs).

The file is 328 MB with about 100 MB of code, so give Ghidra memory: close it, open
`support/launch.properties` in the Ghidra folder, and set `MAXMEM=12G` (use `8G` if you have 16 GB of RAM or less).

## 2. Import and analyse

1. File -> New Project -> Non-Shared Project -> name it `mc`.
2. File -> Import File -> pick `libminecraftpe.so`. Ghidra should say format **ELF**, language **AARCH64:LE:64:v8A**.
3. Double-click the file to open the CodeBrowser. When asked to analyse, click **Yes**, leave the defaults, click **Analyze**.
   This takes a **long** time (30 minutes to a few hours). Wait for the progress bar at the bottom right to finish.

## 3. Find the two functions

Use **Search -> For Strings** (Defined Strings window) and use the Filter box. Double-click a string to jump to it.
In the listing, right-click the string -> **References -> Show References to Address**. Double-click a result to open the code that uses it.

### Target A: render distance slider limits
1. Search `createRenderDistance`. You will see names containing `VideoSettingsFactoryDetails` and `createRenderDistance`.
   These are type names. Follow references until you reach a table of function pointers.
2. Open each function in that table. The one we want contains small whole numbers such as the minimum and maximum
   distance (look for values like `0x5`, `0x60` = 96, `0x80` = 128, or comparisons `cmp w8, #0x..`).
3. Note which function sets the **maximum** value of the slider.

### Target B: chunk radius request
1. Search `RequestChunkRadiusPacket`. Follow references to the code that builds and sends the packet.
2. Find the nearby `cmp`/`csel` (a "min"/"clamp") on the radius value before it is sent.

If you cannot tell which function is right, send both neighbourhoods anyway. I can read them.

## 4. What to send me

For each function (A and B), in the Listing window select the first ~60 instructions of the function, then:

1. Right-click -> **Copy Special... -> Byte String** and paste the result into a text file.
2. Right-click -> **Copy** and paste the disassembly into the same file, so I can read it.
3. Write down the function's **address** (the left column), for example `0d22c274`.

Put everything in one `.txt` file, upload it to your Farion `libs` release (or paste it in the chat if it is small),
and tell me the **Minecraft version** shown in the app store listing.

I will then build the signatures, wire them into `config/signatures.json`, and add the patch to Farion.

## Troubleshooting

* **Out of memory / freezes**: raise `MAXMEM` as above and close other programs.
* **Analysis seems stuck**: it is not; the bar moves slowly on big files. Leave it overnight if needed.
* **Wrong language**: re-import and choose `AARCH64:LE:64:v8A` manually.
