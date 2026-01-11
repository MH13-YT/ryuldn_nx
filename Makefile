KIPS := ryuldn_nx
OVERLAY := overlay

SUBFOLDERS := Atmosphere-libs/libstratosphere $(KIPS) $(OVERLAY)

TOPTARGETS := all clean

OUTDIR		:=	out
SD_ROOT     :=  $(OUTDIR)/sd
TITLE_DIR   :=  $(SD_ROOT)/atmosphere/contents/4200000000000010
OVERLAY_DIR :=  $(SD_ROOT)/switch/.overlays

$(TOPTARGETS): PACK

$(SUBFOLDERS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

$(KIPS): Atmosphere-libs/libstratosphere

#---------------------------------------------------------------------------------
PACK: $(SUBFOLDERS)
	@ mkdir -p $(TITLE_DIR)/flags
	@ mkdir -p $(OVERLAY_DIR)
	@ cp ryuldn_nx/ryuldn_nx.nsp $(TITLE_DIR)/exefs.nsp
	@ cp overlay/overlay.ovl $(OVERLAY_DIR)/ryuldn_nx-overlay.ovl
	@ cp ryuldn_nx/res/toolbox.json $(TITLE_DIR)/toolbox.json
	@ touch $(TITLE_DIR)/flags/boot2.flag
#---------------------------------------------------------------------------------

.PHONY: $(TOPTARGETS) $(SUBFOLDERS)