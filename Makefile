# Top-level convenience — real targets live in app/
.PHONY: run build bundle verify-mp3 verify-psarc models worker

run build bundle verify-mp3 verify-psarc models worker clean:
	$(MAKE) -C app $@
