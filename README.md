# Z EXTERNAL

Aplicativo de console que exibe o nome e informa que o carregamento e a execucao de DLL estao desativados.

O codigo atual nao injeta DLL, nao executa shellcode, nao acessa o processo do Roblox, nao suspende threads e nao cria logs ou arquivos temporarios.

Abra Injector.vcxproj no Visual Studio com as ferramentas C++ e o toolset v145 configurado no projeto. Compile Release x64 para gerar Z EXTERNAL.exe. A compilacao Windows nao foi executada nesta revisao.

Os artefatos antigos nas pastas Release, obj e Injector.tlog sao anteriores a esta alteracao e nao representam o codigo atual. Nao os reutilize; faca uma compilacao limpa.
