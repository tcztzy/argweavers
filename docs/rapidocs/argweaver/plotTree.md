# `plotTree`

Plot a tree


## Description

Plot a tree


## Usage

```r
plotTree(tree, prune = NULL, keepSeqs = NULL, col = "black",
  leafCol = col, leafLabels = NULL, timeScale = 1, drawSpr = FALSE,
  sites = NULL, chromStart = NULL, chromEnd = NULL,
  ylab = "Generations", logScale = FALSE, ylim = NULL, add = FALSE,
  mar = c(8, 4, 1, 1), mod = NULL, cex.leafname = 0.8, ...)
```


## Arguments

Argument      |Description
------------- |----------------
`tree`     |     A newick string containing a tree
`prune`     |     A list of leafs to prune from the tree before plotting (Null means prune none)
`keepSeqs`     |     A list of leafs to keep in the tree (NULL means keep all)
`col`     |     Either a character string (single value) giving color of tree. Or, a list giving color to plot each leaf branch. Internal branches will be plotted as "average" color among child leafs.
`leafCol`     |     If given, this can be a list assigning a color to each leaf name (individual names also work if haploid names are in the form indName_1 and indName_2)
`leafLabels`     |     A list indexed by the leaf names in the tree, giving the label to display for each leaf name. If NULL, the leaf names are displayed.
`timeScale`     |     multiply all branches by this value
`drawSpr`     |     If TRUE, draw the SPR event that turns each tree into the next one
`sites`     |     If given, draw mutation events on tree
`chromStart`     |     The start coordinate of the tree region (1-based; only used if SITES not null)
`chromEnd`     |     The end coordinate of the tree region
`ylab`     |     label for y axis
`logScale`     |     If TRUE, plot in log scale
`ylim`     |     Range for y axis (default; use range of tree)
`add`     |     If TRUE, do not create a new plot
`mar`     |     Margins for plot
`mod`     |     (Advanced; for use with multi-population version of ARGweaver) A model file read in with the function readPopModel, will draw population model underneath tree
`cex.leafname`     |     Character size of leaf names
`...`     |     Passed to plot function


## Note

This creates a new plot for each tree. If plotting to the screen, probably
 want to call par(ask=TRUE) first.
