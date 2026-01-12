KIPS := ryuldn_nx
OVERLAY := overlay

SUBFOLDERS := Atmosphere-libs/libstratosphere $(KIPS) $(OVERLAY)

TOPTARGETS := all clean

OUTDIR		:=	out
SD_ROOT     :=  $(OUTDIR)/sd
TITLE_DIR   :=  $(SD_ROOT)/atmosphere/contents/4200000000000010
OVERLAY_DIR :=  $(SD_ROOT)/switch/.overlays
CONFIG_DIR  :=  $(SD_ROOT)/config/ryuldn_nx

$(TOPTARGETS): PACK

$(SUBFOLDERS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

$(KIPS): Atmosphere-libs/libstratosphere

#---------------------------------------------------------------------------------
PACK: $(SUBFOLDERS)
	@ echo "Packaging ryuldn_nx for SD card..."
	@ mkdir -p $(TITLE_DIR)/flags
	@ mkdir -p $(OVERLAY_DIR)
	@ mkdir -p $(CONFIG_DIR)
	@ cp ryuldn_nx/ryuldn_nx.nsp $(TITLE_DIR)/exefs.nsp
	@ cp overlay/overlay.ovl $(OVERLAY_DIR)/ryuldn_nx.ovl
	@ cp ryuldn_nx/res/toolbox.json $(TITLE_DIR)/toolbox.json
	@ touch $(CONFIG_DIR)/ryuldn_nx.log
	@ touch $(TITLE_DIR)/flags/boot2.flag
	@ echo "Package created in $(OUTDIR)/"
	@ echo "  - Sysmodule: $(TITLE_DIR)/exefs.nsp"
	@ echo "  - Overlay: $(OVERLAY_DIR)/ryuldn_nx.ovl"
	@ echo "  - Config: $(TITLE_DIR)/toolbox.json"
	@ echo "  - Log file: $(CONFIG_DIR)/ryuldn_nx.log (empty)"
	@ echo "  - Boot flag: $(TITLE_DIR)/flags/boot2.flag"
	@ echo ""
	@ echo "Copy the contents of $(OUTDIR)/sd/ to your SD card root."
#---------------------------------------------------------------------------------

.PHONY: $(TOPTARGETS) $(SUBFOLDERS)