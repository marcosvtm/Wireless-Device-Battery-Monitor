# Teste de diagnóstico do Quantum 350

Se o fone aparecer no Wireless Battery e a porcentagem bater com o JBL QuantumENGINE, basta confirmar a leitura. Se o fone sumir ou a porcentagem divergir, abra o teste portátil, mantenha o receptor conectado e siga as instruções. O botão **Mostrar arquivo** seleciona o relatório na pasta Downloads.

O teste reconhece o receptor `0ECB:206B`, interface 3. Ele registra os relatórios de entrada, os identificadores descritos pelo Windows e as respostas às consultas de conexão e bateria do modelo. A porcentagem digitada no início serve de referência para comparar os resultados.

Para compilar no Windows, coloque o LLVM-MinGW x64 no PATH e execute `./build.ps1` nesta pasta. Para verificar as duas coleções observadas com dados simulados, compile `check.cpp` com a opção `-I../../src`, o recurso `dialog.o` e as bibliotecas `shlwapi`, `shell32`, `ole32`, `uuid` e `hid`.
