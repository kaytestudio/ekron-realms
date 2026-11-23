**Ekron — Sandbox Urbana**

**Direção Acordada**
Jogo para Android na primeira pessoa: exploração livre de uma cidade fictícia em tempo de guerra e construção. Não existem missões, campanha de história, marcadores de objetivos nem sequência obrigatória de ações.
Referência visual: `concepts/ekron-war-city-ru.png`. Isto é arte conceptual e não uma captura de ecrã do motor. As missões e o marcador de distância apresentados na imagem NÃO fazem parte do jogo.

**Mundo**

* Cidade parcialmente destruída: quarteirões intactos, edifícios danificados, escombros e carros abandonados.
* Os edifícios acessíveis têm entradas e interiores reais, não apenas fachadas.
* É possível colocar mobília e elementos de construção nos espaços interiores.
* Mundo aberto é a direção pretendida; o primeiro protótipo técnico limita-se a um pequeno quarteirão.

**Sandbox**

* Movimentação livre sem missões, temporizadores ou objetivos obrigatórios.
* Catálogo de construção: parede, chão, janela, porta, mesa, cadeira, armário, sofá.
* Seleção de item, pré-visualização semitransparente, rotação, confirmação de colocação e cancelamento.
* Seleção e eliminação de itens colocados pelo jogador.
* Guardar e carregar objetos colocados.
* No primeiro protótipo, os itens de construção estão disponíveis sem recolha de recursos: esta é uma decisão técnica inicial e não a economia final do jogo.

**Controlos e Idioma**

* Interface, nomes dos itens e dicas em quatro idiomas: inglês, russo, português europeu e espanhol (Espanha). O idioma segue as definições do sistema (no computador também `--lang`, `EKRON_LANG`, a tecla `L` e o menu de pausa).
* Telemóvel na orientação horizontal.
* À esquerda — joystick de movimento; à direita — área de visão e botões de interação, salto e construção.
* No modo de construção — seleção de item, rotação, colocação e cancelamento.
* Não mostrar registo de missões nem marcadores de objetivos.

**Primeira Etapa Verificável de Implementação**

1. Compilação Android executável e controlos de movimento/câmara.
2. Pequeno quarteirão com colisões e um interior acessível.
3. Colocação, rotação e eliminação de um tipo de objeto de construção.
4. Guardar o estado e restaurá-lo após reiniciar.
5. Expansão do catálogo e preenchimento do quarteirão.

**Limites das Alterações Atuais**
O protótipo de navegador foi removido: o utilizador confirmou que são necessárias alterações diretamente no jogo Android. Foi modificado o projeto nativo existente `sys/android`: GameActivity → JNI → C++/OpenGL ES 3. Foram adicionados um pequeno quarteirão com interiores acessíveis, movimento com colisões, visão tátil, oito tipos de itens colocáveis, rotação, eliminação e gravação num ficheiro privado da aplicação. WebView, JavaScript e npm não são utilizados na compilação Android. Estas alterações pertencem ao ramo de código Android; o motor Pascal para computador não é afetado por elas. Instruções: `sys/android/README.md`.
Os gráficos consistem por enquanto em primitivas e não reproduzem o nível de detalhe do conceito. A imagem gerada não substitui modelos 3D, texturas e otimização para telemóvel. A compilação Android ainda não foi testada. Multijogador, destrutibilidade total dos edifícios e NPCs de combate ainda não estão acordados.
