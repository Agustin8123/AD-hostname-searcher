========================================================================
 Buscador de nombres de host disponibles en Active Directory

QUE HACE ESTE SCRIPT
---------------------
1. Consulta Active Directory (via PowerShell) para obtener todos los
   objetos de computadora registrados en el dominio.
2. Reconoce la nomenclatura reglamentaria:
       ATZZTEPC000xxx   -> equipos de escritorio (familia PC)
       ATZZTENB000xxx   -> notebooks (familia NB)
   donde "000" es fijo y "xxx" es el numero de maquina.
3. Detecta los numeros "huecos" (no usados) dentro de cada familia: los
   nombres que, segun AD, estarian disponibles para un equipo nuevo.
4. Hace una verificacion complementaria por red (ping), tanto de los
   nombres ocupados como de los huecos, SOLO como dato extra: la
   decision de "disponible / ocupado" se basa siempre en Active
   Directory, nunca en si responde o no el ping.

QUE NO HACE (a proposito)
----------------------------
- No modifica nada en Active Directory. Solo lee (Get-ADComputer).
- No guarda ni pide contrasenas: usa la sesion de Windows actual.

ARCHIVOS INCLUIDOS
---------------------
- disponibilidad_hostnames_ad.py   El script principal.
- Abrir_TUI.bat                    Doble click para abrir el menu
                                    interactivo (--tui) sin escribir nada
                                    en una terminal. Tiene que estar en
                                    la misma carpeta que el .py.
- readme.txt                       Este archivo.

REQUISITOS PARA USARLO CONTRA UN AD REAL
--------------------------------------------
- Windows, en un equipo unido al dominio (o con el modulo de PowerShell
  "ActiveDirectory" / RSAT instalado).
- Python 3.9 o superior (no usa librerias externas, solo la libreria
  estandar). Link de descarga: https://www.python.org/downloads/
- Una cuenta de dominio con permisos de LECTURA sobre los objetos de
  computadora (la mayoria de las cuentas normales ya los tienen).

COMO PROBARLO HOY MISMO, SIN UN AD REAL (MODO DEMO)
-------------------------------------------------------
    python disponibilidad_hostnames_ad.py --demo --sin-red

Esto usa una lista de nombres fija (identica al ejemplo del documento de
la pasantia) en lugar de llamar a PowerShell, y "--sin-red" evita los
pings (que de todas formas fallarian contra nombres que no existen en tu
red). Sirve para validar el reconocimiento de nomenclatura y la deteccion
de huecos antes de tener el laboratorio de AD armado.

USO NORMAL, CONTRA UN AD REAL
----------------------------------
    python disponibilidad_hostnames_ad.py

MODO INTERACTIVO (TUI), SI NO QUERES MEMORIZAR FLAGS
----------------------------------------------------------
    python disponibilidad_hostnames_ad.py --tui

O directamente doble click en Abrir_TUI.bat.

Abre un menu que pregunta paso a paso: fuente de datos (demo o AD real),
si verificar la red, que rango de numeros analizar (automatico, completo,
o un tramo especifico como 020-124), y si mostrar todo o solo lo libre.
Al final arma exactamente los mismos parametros que se pasarian por
linea de comandos y llama a las mismas funciones de siempre: no hay
logica distinta entre los dos modos, solo una forma mas de usarlo.

OPCIONES
-----------
    --tui           Abre el menu interactivo de arriba, en vez de leer
                     los flags de abajo.
    --demo          Usa datos de ejemplo en vez de consultar AD real.
    --sin-red       No hace ping a los equipos (mas rapido).
    --maximo N      Fuerza el limite superior de busqueda de huecos a N
                     (equivale a --rango NUMERO_MINIMO-N). No se combina
                     con --rango.
    --rango I-F     Busca huecos solo entre I y F, por ejemplo 020-124,
                     sin importar el numero mas alto que haya en AD. No
                     se combina con --maximo.
    --solo-libres   En el listado, muestra unicamente los nombres libres.

SOBRE EL RANGO NUMERICO
----------------------------
El tope no esta hardcodeado como "999" suelto por el codigo: sale de una
sola constante, DIGITOS_NUMERO (seccion 1 del archivo), que hoy vale 3
porque asi es la nomenclatura real. A partir de ahi se calculan solos
NUMERO_MINIMO (1) y NUMERO_MAXIMO (999 mientras sean 3 digitos), y de ahi
toman su valor el patron de reconocimiento y la validacion de --maximo/
--rango. Si el dia de manana la empresa pasa a usar mas digitos, se
cambia DIGITOS_NUMERO una sola vez arriba del archivo y todo lo demas se
ajusta solo.

Aparte del largo del numero, conviene tener claro hasta donde busca
huecos por defecto: sin --maximo ni --rango, el script busca solamente
entre el minimo y el numero mas alto que YA existe en AD. Por ejemplo, si
los mas altos registrados son ATZZTEPC000001..005, busca huecos entre 001
y 005 (en este caso, 004). No va a sugerir 006 en adelante, porque esos
numeros todavia no son un "hueco": son simplemente el siguiente tramo sin
usar. Para cubrir un tramo especifico en cambio de todo lo registrado en
AD, se puede pedir un rango exacto:

    python disponibilidad_hostnames_ad.py --rango 020-124

Y para cubrir toda la nomenclatura posible de una, sin depender de nada
de lo que haya en AD:

    python disponibilidad_hostnames_ad.py --maximo 999

SOBRE LA CANTIDAD DE EQUIPOS
----------------------------------
Consultar AD y reconocer la nomenclatura es practicamente instantaneo
aunque haya cientos o miles de equipos (es una sola consulta a
PowerShell mas comparaciones de texto). Lo unico que podria ser lento es
el ping de verificacion, asi que TODOS los pings se lanzan en paralelo
(ver MAX_PINGS_CONCURRENTES) en lugar de hacerse uno por uno. Aun asi,
para un escaneo rapido sin tocar la red, --sin-red sigue siendo la
opcion mas veloz.
========================================================================
