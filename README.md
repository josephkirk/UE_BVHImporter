# BVH Importer for UE5

Just a simple plugin I whipped up to get `.bvh` motion capture files into Unreal Engine 5 for processing mocap dataset. Also to learn how to build custom importers for UE5.

## What it does
You drag a BVH file into the content browser, and it spits out:
- A Skeleton
- A dummy Skeletal Mesh (it's just a triangle, don't judge)
- The Animation Sequence
- Dragging multiple BVH files will create a sequence for each file sharing the same skeleton.
- The importer find any skeleton in the import folder to use for the imported animation, so make sure the skeleton in import folder is the one you want to use when dragging multiple BVH files.

## Why?
I needed to bulk import some mocap data and didn't want to deal with retargeting or external tools for every single file. This just automates the boring stuff.

## How to use
1. Drop the `BVHImporter` folder into your project's `Plugins` folder.
2. Compile.
3. Drag & drop your `.bvh` files.
4. Done.

## Notes
- It converts the coordinates from BVH (Y-up) to UE (Z-up) automatically.
- The mesh is super basic, just enough to make the animation asset valid. You'll probably want to retarget the animation to your actual character anyway.
- Works on my machine with Unreal Engine 5.6. Feel free to fork it if you need more features or create issue/feature request on project github.
- Tested on Bandai Namco and 1000 Styles mocap datasets.
