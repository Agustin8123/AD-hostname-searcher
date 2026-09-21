# AD Hostname Searcher

Aplicación gráfica nativa para Windows 10/11 x64, escrita en C++17. Consulta los equipos de Active Directory, detecta huecos de numeración y verifica DNS/ping en paralelo. No incluye modo de demostración ni necesita Python, PowerShell o RSAT para funcionar.

## Abrir la aplicación

Ejecutar `dist/ADHostnameSearcher.exe` con doble clic. Es portable; puede copiarse a otra PC Windows. Usa la cuenta de Windows actual y solo lee AD. Se necesita conexión al dominio (o VPN) y permisos de lectura. No solicita credenciales ni modifica el directorio.

1. Configurar la nomenclatura: **prefijo + familia + bloque fijo + número**. Por ejemplo, para `ATZZTEPC000042`: prefijo `ATZZTE`, bloque fijo `000`, 3 dígitos. La familia `PC` se descubre automáticamente; no existe una lista fija de familias en el código.
2. Elegir rango automático, completo o personalizado. El automático va desde 0 hasta el mayor número registrado de cada familia; el personalizado incluye ambos extremos. Un equipo registrado con número 0 se considera ocupado.
3. Pulsar **Consultar AD**. Se cargan todas las familias detectadas que respeten la nomenclatura. Los nombres fuera del formato se cuentan en el estado inferior y no se usan para generar huecos.
4. Marcar una, varias o todas las familias. La selección funciona con casillas, sin necesidad de Ctrl. La búsqueda de nombre/IP y el filtro de estado se aplican junto con las familias.
5. Pulsar **Verificar ping + IP** para verificar todas las filas de las familias seleccionadas, incluidos los huecos. Los filtros de texto/estado solo afectan la visualización. La IP aparece inmediatamente a la derecha del dispositivo al terminar la operación.
6. **Ver resumen** muestra el primer nombre candidato de cada familia seleccionada y la lista de equipos fuera de nomenclatura. El texto se puede seleccionar y copiar.

Para cambiar formato o rango después de consultar, usar **Aplicar formato y rango**. Reutiliza la consulta y borra verificaciones de red anteriores. **Consultar AD** vuelve a leer el directorio. El formato se guarda por usuario en `%LOCALAPPDATA%\ADHostnameSearcher\settings.ini`; no se guarda el inventario.

## Interpretación de resultados

- **Registrado**: el nombre existe en AD, independientemente de que responda al ping.
- **No registrado**: hueco en AD. Revisar antes de asignar; no representa una reserva de nombre.
- **Conflicto**: un nombre no registrado en AD responde a ICMP.
- **Sin respuesta**: no respondió a la prueba; no implica que esté apagado o que el nombre esté libre.
- **Sin IPv4 / DNS**: no se obtuvo una dirección IPv4. La prueba de red de esta versión es IPv4.
- **Error de red**: falló la resolución o la operación ICMP; no se interpreta como disponibilidad.

Se conservan todas las IPv4 resueltas aunque el ping no responda. Para equipos registrados se usa `dNSHostName` si AD lo proporciona; para huecos se usa el nombre generado con los sufijos DNS configurados en Windows. Si hay varias IPv4 se muestran juntas y se informa respuesta si alguna contesta. Antes de ejecutar ping, la columna IP muestra un guion.

## Rendimiento y cancelación

Consulta LDAP nativa paginada, sin iniciar procesos externos, tabla virtual y hasta 30 tareas DNS/ICMP simultáneas. El ping espera hasta 500 ms por dirección. La resolución DNS y el acceso al dominio dependen de la red y pueden tardar más; migrar a C++ no elimina estos tiempos. Las operaciones se ejecutan fuera del hilo de la interfaz.

Cancelar deja de programar tareas nuevas y espera las llamadas de Windows en curso; conserva el informe anterior. Cerrar durante una consulta también espera esas llamadas. Las consultas fallidas o incompletas no se presentan como un inventario vacío. El informe está limitado a 250.000 filas para evitar consumos excesivos; para rangos mayores se debe reducir el intervalo.

## Compilar

Con un toolchain MinGW-w64 que incluya C++17 y `windres`:

```powershell
./build.ps1 -Toolchain C:\ruta\w64devkit\bin
```

El script compila, ejecuta las pruebas de lógica y genera `dist/ADHostnameSearcher.exe` enlazado estáticamente con el runtime C++. El toolchain no se descarga automáticamente ni se incluye en Git.

Alternativamente, con Visual Studio Build Tools (C++ y Windows SDK) y CMake:

```powershell
cmake -S . -B build/vs -A x64
cmake --build build/vs --config Release
ctest --test-dir build/vs -C Release --output-on-failure
```

En ese caso el ejecutable está en `build/vs/Release/ADHostnameSearcher.exe`.

Las pruebas cubren familias dinámicas, formato, límites de rango, huecos, número cero, conservación del FQDN y clasificación de conflictos. La consulta y el ping de equipos corporativos necesitan una comprobación adicional en el dominio real.

Implementación basada en [consultas paginadas ADSI](https://learn.microsoft.com/en-us/windows/win32/adsi/paging-with-idirectorysearch) y [ICMP de Windows](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmpsendecho).
