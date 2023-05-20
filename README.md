# Infinity Automata 
A little infinite-grid cellular automata engine
## Features:
  * Smooth Scrolling
  * Optimized Software Renderer (Runs at under 4ms frametime with over 7000 live cells on screen)
  * Infinite Canvas  
  * SIMD and Multithreading for Rendering and Grid Processing
  * No runtime heap allocations. (custom memory arena for each system pre-allocates memory at start)
  
Note: Currently simulates Conway's GOF, Sand and Brick.
![Demo](renderer_new3.gif)

