# -----------------------------------------------------------------------------
# Packaging definition.
# -----------------------------------------------------------------------------

element = object {
  param contents : list[target] = []
  param location : string
}

# TODO: Need a better name for this.
elements = {
  'programs' = element {
    location = 'bin'
  },
  
  'libs' = element {
    location = 'lib'
  },
  
  'data' = element {
    location = "share"
  },
}

# -----------------------------------------------------------------------------
# A package is a collection of installable items, along with a set of metadata
# describing the items. The contents of a package are grouped by installation
# location - that is, items which will be installed together are grouped
# together.
# -----------------------------------------------------------------------------

package = object {
  param name : string
  param version : string
  param summary : string = ''
  param description : string = ''
  param authors : list[string]
  param homepage : string

  param contents : list[element]
}
