# BVH Importer for UE5

Just a simple plugin I whipped up to get `.bvh` motion capture files into Unreal Engine 5 without the usual headache.

## What it does
You drag a BVH file into the content browser, and it spits out:
- A Skeleton
- A dummy Skeletal Mesh (it's just a triangle, don't judge)
- The Animation Sequence

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
- Works on my machine. Feel free to fork it if you need more features.
