# Projeto SO
## Primeiramente, deve certificar-se que no seu projeto está estruturado da seguinte maneira depois de fazer make:

	ProjetoSO/
	┣ cFiles/
	┃ ┣ cliente.c
	┃ ┣ servidor.c
	┃ ┗ util.c
	┣ configs/
	┃ ┣ cliente.conf
	┃ ┣ clienteParcial.conf
	┃ ┗ servidor.conf
	┣ headers/
	┃ ┣ cliente.h
	┃ ┣ servidor.h
	┃ ┗ util.h
	┣ logs/
	┣ oFiles/
	┃ ┣ cliente.o
	┃ ┣ servidor.o
	┃ ┗ util.o
	┣ .gitignore
	┣ README.md
	┣ cliente
	┣ jogosEsolucoes.txt
	┣ makefile
	┗ servidor

## Para iniciar o servidor é necessário passar o caminho do ficheiro que será usado para a configuração do servidor. Com o comando ./servidor ./configs/servidor.conf . Deverá certificar se que o ficheiro contém a seguinte estrutura,uma linha para cada parâmetro:

jogosEsolucoes.txt
25575
200

Primeira linha-nome do ficheiro que contém os jogos cuja localização é obrigatória ser a raiz do projeto.
Segunda linha-porta que será utilizada na comunicação socket (IPC).
Terceira linha-número de clientes máximo na fila singleplayer.
O ficheiro que contém os jogos deve ter o seguinte formato:

	0
	5300 … com 81 digitos apenas
	5346 … com 81 digitos apenas

Primeira linha-id do jogo que será o mesmo id da sala.
Segunda linha-jogo que será enviado ao cliente.
Terceira linha-solução do jogo.

## Para iniciar os clientes é necessário passar o caminho do ficheiro que será usado para a configuração do cliente. Com o comando ./cliente ./configs/cliente.conf . Deverá certificar se que o ficheiro contém a seguinte estrutura,uma linha para cada parâmetro:

SIG
COMPLET
127.0.0.1
25575
25
1500

Primeira linha-se é desejado criar clientes no modo singleplayer (SIG) ou no modo multiplayer (MUL).
Segunda linha-resolução que deverá ser usada completa (COMPLET) ou parcial (PARCIAL).
Terceira linha-IP do servidor que irá validar soluções.
Quarta linha-porta desse servidor.
Quinta linha-número de clientes que pretende-se simular com a mesma configuração mas IDs diferentes.
Sexta linha-tempo entre cada tentativa do cliente em ms.
