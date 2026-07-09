# Relevamiento de componentes - front-end electrico ARINC 429

Fecha de relevamiento: 2026-07-09
Proyecto: PAMPA / ARINC 429 - etapa electrica de laboratorio
Alcance: componentes para adaptar una senal logica de Raspberry Pi Pico a un banco bipolar de laboratorio y recuperar la senal hacia logica 0-3.3 V. La Raspberry Pi Pico no se incluye porque ya esta disponible.

## Criterio usado

- Primero se buscaron componentes con precio publicado en Cordoba.
- Si no aparecieron en Cordoba, se uso Buenos Aires / CABA.
- Si tampoco hubo precio local verificable, se uso fabricante o distribuidor oficial en USD.
- No se cargo ningun precio "a consultar" como precio final.
- MercadoLibre no se pudo verificar desde este entorno: la API devolvio 403, la pagina normal devolvio verificacion de trafico y los buscadores publicos no entregaron resultados indexados utiles para Tecnoliveusa. Queda pendiente incorporar publicaciones concretas si se reciben links o capturas.

## Fuentes y contactos

### Electrocomponentes

Sitio: https://www.electrocomponentes.com/
Contacto: https://www.electrocomponentes.com/tienda/contacto
Puntos de venta: https://www.electrocomponentes.com/tienda/puntos-de-venta

- Sucursal Cordoba: Rivera Indarte 334, Cordoba. Tel. (0351) 422-0896.
- Casa central CABA: Solis 225, C1078AAE, CABA. Tel. (5411) 4375-3366 / 4372-1864.
- Sucursal Liniers: Cosquin 77, CABA. Tel. (5411) 4641-1223.
- WhatsApp publicado: +54 9 11 5379 1151.

### Dicomse

Sitio: https://www.dicomse.com.ar/
Direccion: Doblas 1126, CABA.
Telefono: (5411) 4923-1945 / (5411) 4922-1601.
WhatsApp: 1125034238.
Email publicado: info@dicomse.com.ar.

Condiciones publicadas:

- Precios en dolares estadounidenses.
- Valor de referencia indicado por el sitio: USD 1 = ARS 1400.
- IVA: 10.5% en semiconductores y 21% en material general.
- Envio minimo informado: ARS 20000.
- Moto Capital: ARS 6000.
- Partidos limitrofes CABA: ARS 8900.
- La Plata: ARS 11500.
- Correo Buenos Aires: ARS 9500.
- Otras provincias hasta 1000 km: ARS 10500.

### Todomicro

Sitio: https://www.todomicro.com.ar/

- Villa Urquiza: Av. Triunvirato 4135 piso 4 oficina 58, CABA. Tel. 011 5263-7073.
- Microcentro: Galeria Central, Av. Corrientes 640 piso 4 oficina 7, CABA. Tel. 011 5263-9793.
- Villa Crespo: Av. Juan B. Justo 3046, CABA. Tel. 011 7078-1286.
- Lineas rotativas: +54 11 7078-1280.
- Email: info@todomicro.com.ar.
- Soporte: soporte@todomicro.com.ar.
- WhatsApp ventas: +54 11 7364 3463.
- WhatsApp mayorista: +54 9 11 2573 0643.
- WhatsApp tecnica: +54 9 11 2251 3620.
- WhatsApp logistica: +54 11 2253 3294.
- Envio gratis informado para compras superiores a ARS 33000.

## Busqueda MercadoLibre / Tecnoliveusa

Se realizo una busqueda adicional porque se observo que varios componentes
podrian estar publicados en MercadoLibre por el vendedor `Tecnoliveusa`.

Resultado verificable desde este entorno:

| Via consultada | Consultas usadas | Resultado | Decision |
|---|---|---|---|
| Bing RSS indexado | `MCP6562 Tecnoliveusa`, `LMV393 Tecnoliveusa`, `LMV331 Tecnoliveusa`, `TLV3702 Tecnoliveusa`, `TLV9352 Tecnoliveusa`, `OPA2197 Tecnoliveusa`, `OPA197 Tecnoliveusa`, `MCP6002 Tecnoliveusa` | No devolvio resultados filtrables que mencionen MercadoLibre/Tecnoliveusa junto con el componente. | No se agregan precios. |
| Bing RSS indexado | mismos componentes + `MercadoLibre Argentina` | No devolvio resultados filtrables utiles. | No se agregan precios. |
| Google HTML | busqueda exacta `"MCP6562" "Tecnoliveusa"` | La consulta devolvio pagina de bloqueo/challenge y no resultados utilizables. | No se usa como fuente. |
| DuckDuckGo HTML | `Tecnoliveusa MCP6562 Mercado Libre` | La consulta devolvio challenge anti-bot. | No se usa como fuente. |
| API publica MercadoLibre | `https://api.mercadolibre.com/sites/MLA/search?q=MCP6562` | Respuesta HTTP/API `403 forbidden`. | No se usa como fuente. |
| Listado publico MercadoLibre | `https://listado.mercadolibre.com.ar/mcp6562` | Redireccion a verificacion de cuenta/trafico sospechoso. | No se usa como fuente. |

Conclusion: en esta revision no se pudo verificar precio, vendedor ni link
directo de Tecnoliveusa por resultados indexados publicos. Si se obtiene un
link concreto de una publicacion o una captura con precio, conviene incorporarlo
como evidencia puntual. Hasta entonces se mantiene la decision anterior: no
cargar precios de MercadoLibre no verificables.

## Correspondencia con bloques del esquematico KiCad

El esquematico KiCad usado como referencia esta en:

- `ELECTRICO/frontend_arinc429_lab/frontend_arinc429_lab.kicad_sch`
- `ELECTRICO/frontend_arinc429_lab/exports/frontend_arinc429_lab.svg`

La tabla siguiente indica que componente del relevamiento corresponde a cada bloque funcional del esquematico.

| Bloque KiCad | Referencias / nets | Funcion | Componentes asociados | Recomendacion |
|---|---|---|---|---|
| `J1 - Raspberry Pi Pico de prueba` | `GP2`, `GP3`, `GP4`, `GP5`, `3V3`, `GND` | Punto de entrada/salida logico del banco. | Raspberry Pi Pico ya disponible, tira de pines 1x40, cables Dupont, protoboard. | No hace falta comprar otra Pico. Si se arma prolijo, soldar pines macho y usar borneras/headers para no depender de cables flojos. |
| `TX bipolar` | `U1A`, `U1B`, `+6V_TX`, `-6V_TX` | Convierte `TX_A_LOGIC/TX_B_LOGIC` en `LINE_A/LINE_B` bipolar. | TL072CP, TL082CP, LM358N, OPA2197, TLV9352, resistencias 10k/20k/30k, capacitores 100 nF. | Para banco local: TL072/TL082. Mejor tecnico: OPA2197/TLV9352 si se importan. Evitar LM358 como solucion final. |
| `U1A op-amp diferencial` | `LINE_A = 1,5*(TX_A - TX_B)` | Genera el conductor A con ganancia 1,5. | Op-amp dual, resistencias de ganancia `Rin=20k`, `Rf=30k`, desacople `C1..C6`. | Requiere resistencias preferentemente 1%. Con 5% funciona para ensayo, pero cambia la amplitud. |
| `U1B inversor` | `LINE_B = -LINE_A` | Genera el conductor B invertido. | Segunda mitad del mismo op-amp dual, resistencias 10k/10k. | Debe usar resistencias iguales para que `LINE_B` sea espejo de `LINE_A`. |
| `J2 - Linea A/B` | `LINE_A`, `LINE_B`, `GND_REF` | Conector del par diferencial de banco. | Borneras 2 o 3 vias, pin headers, TVS de linea. | Usar bornera 3 vias para A, B y GND_REF. La sniffer o medicion se conecta al cable/linea, no a la placa como si fuera una salida dedicada. |
| `A_SENSE` / `B_SENSE` | `LINE_A -> 22k -> nodo`, `33k a GND`, clamps | Baja y protege cada conductor antes del comparador. | Resistencias 22k/33k, 1N4148, BAV99, BAT54, 1N5819, TVS. | Para laboratorio alcanza divisor + clamps. Para campo real se debe endurecer con proteccion y receptor diferencial mas serio. |
| `U2A MCP6562` | `A_SENSE > VTH -> RX_A_LOGIC / GP4` | Recupera el canal A como logica 0-3,3 V. | MCP6562 ideal si se consigue, LM393N como alternativa local, LMV393/LMV331 si aparecen, TLV3702 solo como alternativa lenta. | Mejor opcion tecnica: MCP6562/LMV393. Opcion comprable local: LM393N con pull-up a 3V3. |
| `U2B MCP6562` | `B_SENSE > VTH -> RX_B_LOGIC / GP5` | Recupera el canal B como logica 0-3,3 V. | Mismo integrado dual que U2A. | Conviene usar un comparador dual para que ambos canales tengan comportamiento parecido. |
| `R11/R12 - Umbral RX` | `+3V3 -> R11 12k -> VTH_COMP -> R12 10k -> GND` | Genera `VTH_COMP`, umbral aproximado de 1,50 V. | Resistencias 12k y 10k, idealmente 1%. | Si se usan 5%, medir el umbral real con multimetro. Si hay ruido, agregar histeresis despues de medir. |
| `J3 - Fuentes y referencias` | `+3V3_LOGIC`, `+6V_TX`, `-6V_TX`, `GND_COMUN` | Entrada de alimentacion y referencia comun. | Fuente de laboratorio dual, ICL7660, LM2596, MT3608, capacitores 10 uF y 100 nF. | Para primeras pruebas, fuente dual de laboratorio es lo mas claro. ICL7660 sirve para baja corriente, no para cargar una linea pesada. |
| `C1..C6` | Desacople cerca de `U1/U2` | Estabiliza alimentacion y reduce ruido local. | Capacitores 100 nF ceramicos, 10 uF electroliticos. | Colocar 100 nF cerca de cada IC y 10 uF por riel o por modulo de alimentacion. |

## Intercambiabilidad y jerarquia tecnica

### Comparadores RX (`U2A/U2B`)

| Opcion | Sirve para | Ventaja | Limitacion | Decision recomendada |
|---|---|---|---|---|
| MCP6562 | RX principal si se consigue | Comparador moderno para baja tension, mas adecuado para logica de 3,3 V. | No se verifico precio local. Antes de comprar confirmar encapsulado, pinout y tipo de salida. | Mejor candidato tecnico para reemplazar al LM393. |
| LM393N | Banco de laboratorio y pruebas inmediatas | Disponible localmente, barato, conocido, suficientemente rapido para este banco. | Salida open collector: necesita pull-up a 3V3. No es rail-to-rail ni tan moderno. | Comprar para avanzar ya. Pull-up tipico: 4k7 a 3V3, nunca a 5V hacia la Pico. |
| LMV393 | Alternativa moderna si aparece | Variante de baja tension mas natural para 3,3 V. | No se encontro precio local verificable. | Buena alternativa si aparece en MercadoLibre o tienda local. |
| LMV331 | Alternativa simple por canal | Util si solo se consigue comparador simple. | Harian falta dos unidades/canales y revisar pinout. | Usar solo si no se consigue dual. |
| TLV3702 | Pruebas lentas o bajo consumo | Nanopower y baja tension. | Demasiado lento para margen comodo a 100 kbps. | No usar como primera opcion para ARINC 100 kbps. |

### Driver TX bipolar (`U1A/U1B`)

| Opcion | Sirve para | Ventaja | Limitacion | Decision recomendada |
|---|---|---|---|---|
| OPA2197 | Driver analogico mas serio | Mejor precision, comportamiento mas robusto y especificacion moderna. | No se encontro local; precio TI en USD y envio/importacion aparte. | Mejor opcion si se va a comprar afuera. |
| TLV9352 | Driver moderno si hay stock | Precio TI bajo y especificacion moderna. | Figuraba sin stock en la consulta y no hay precio local. | Valido si aparece disponible. |
| TL072CP / TL082CP | Banco con fuente dual | Disponibles en Electrocomponentes, sirven para ensayar la idea con `+6V/-6V`. | No son rail-to-rail; revisar amplitud real y carga. | Opcion local razonable para prototipo. |
| LM358N | Ensayo basico de baja exigencia | Muy barato y facil de conseguir. | Limitado en velocidad y swing de salida; puede deformar amplitudes. | No usar como driver final del TX bipolar. |

### Alimentacion (`J3`, `+6V_TX`, `-6V_TX`)

| Opcion | Sirve para | Ventaja | Limitacion | Decision recomendada |
|---|---|---|---|---|
| Fuente de laboratorio dual | Validacion inicial | Permite ajustar `+6V/-6V`, medir corriente y descartar fallas del convertidor. | No es una solucion embebida final. | Primera opcion para probar el esquema. |
| ICL7660 / TC7660 | Generar riel negativo de baja corriente | Barato, simple, disponible en Dicomse. | Corriente limitada y ruido de conmutacion; no alimenta cargas grandes. | Usar si el op-amp y la carga consumen poco. Medir rizado. |
| LM2596 | Bajar una tension positiva | Disponible localmente. | No genera `-6V`. | Complementario, no reemplaza la fuente dual. |
| MT3608 | Subir una tension positiva | Disponible como modulo barato. | No genera negativo por si solo. | Complementario, no solucion completa. |
| Modulo DC/DC bipolar | Producto compacto si se consigue | Solucion directa para `+V/-V`. | No se encontro precio local verificable. | Buscar como modulo especifico o usar fuente dual al principio. |

### Proteccion de linea (`J2`, `A_SENSE`, `B_SENSE`)

| Opcion | Sirve para | Ventaja | Limitacion | Decision recomendada |
|---|---|---|---|---|
| TVS P6SMB/P6KE/SMAJ | Proteccion contra transitorios | Mas robusto frente a picos. | Elegir tension segun nodo: no todos sirven en todos los puntos. | Usar cerca de entrada/salida de linea. |
| BAT54 / 1N5819 | Clamp rapido de laboratorio | Baja caida directa, util para limitar nodos sensibles. | No reemplaza una proteccion industrial completa. | Util en prototipo RX. |
| BAV99 / 1N4148 | Diodos rapidos de senal | Baratos y faciles de usar. | Mayor caida que Schottky. | Buenos para pruebas y clamps simples. |
| Solo divisor resistivo | Primera prueba sin proteccion fuerte | Simple y barato. | No protege ante errores de conexion o transitorios. | No dejarlo asi si se conectara a hardware externo. |

### Resistencias y capacitores

| Bloque | Valor | Componente ideal | Alternativa encontrada | Comentario |
|---|---|---|---|---|
| `U1A` ganancia | 20k / 30k | Resistencias 1% | Resistencias 5% locales o kit 1% externo | La relacion define la amplitud de `LINE_A`. Mejor 1%. |
| `U1B` inversor | 10k / 10k | Resistencias 1% emparejadas | 5% para prueba | Si no son iguales, `LINE_B` no queda perfectamente espejada. |
| `A_SENSE/B_SENSE` | 22k / 33k | Resistencias 1% | 5% para prueba | Determinan cuanto baja la senal antes del comparador. |
| `R11/R12` umbral | 12k / 10k | Resistencias 1% | 5% midiendo `VTH_COMP` | `VTH_COMP` debe quedar cerca de 1,50 V. |
| `C1..C6` | 100 nF | Ceramico cerca de cada IC | Electrocomponentes 0603/0805 o poliester TH | No omitir desacople. |
| Filtro por riel | 10 uF | Electrolitico o tantalio | Electrocomponentes 10 uF 25/50 V | Colocar por riel y cerca del convertidor/fuente. |

## Componentes con precio local verificado

### Comparadores y operacionales

| Componente | Fuente | Precio | Link | Nota |
|---|---:|---:|---|---|
| LM393N 2x comparador DIP8 | Electrocomponentes | ARS 390 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/lm393n-2x-comparador-voltaje | Comparador local practico para RX de banco. Salida open collector. Requiere pull-up. |
| LM393H 2x comparador TO-5 | Electrocomponentes | ARS 5520 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/lm393h-2x-comparador-voltaje | Mismo concepto, encapsulado menos practico para protoboard. |
| LM358SMD FRAC 2x operacional | Electrocomponentes | ARS 165 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/lm358smd-frac-2x-amplificador-operacional | Alternativa economica para pruebas lentas. No ideal para driver final. |
| LM358N 2x operacional DIP8 | Electrocomponentes | ARS 579 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/lm358n-2x-amplificador-operacional | Facil de probar en protoboard, pero limitado para senales rapidas y swing cercano a rieles. |
| TL082CP 2x operacional JFET | Electrocomponentes | ARS 861 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/tl082cp-2x-amplificador-operacional-entrada-jfet | Alternativa local para pruebas analogicas con fuente dual. |
| TL072CP 2x operacional JFET | Electrocomponentes | ARS 898 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/circuitos-lineales/tl072cp-2x-amplificador-operacional-entrada-jfet | Alternativa local similar al TL082. |
| LM393 | EB Electronica | ARS 1430 | https://www.ebelectronica.com.ar/componentes-electronicos/circuitos-integrados/lm393 | Referencia secundaria. |
| LM393 | SDV Electronica | ARS 1099.89 | https://sdvelectronica.com.ar/producto/lm393/ | Referencia secundaria. |

### Alimentacion y conversion

| Componente | Fuente | Precio | Link | Nota |
|---|---:|---:|---|---|
| LM2596S-ADJ step-down | Electrocomponentes | ARS 4448 | https://www.electrocomponentes.com/tienda/componentes-electronicos/circuitos-integrados-c01s39s06/reguladores-de-tension/switching/regulador-tension-lm2596s-adj-step-down-1237v | No genera fuente bipolar. Sirve para bajar tension positiva. |
| ICL7660AIBA | Dicomse | USD 4.00 + IVA | https://www.dicomse.com.ar/ | Inversor/carga capacitiva para generar tension negativa de baja corriente. |
| ICL7660CPA | Dicomse | USD 5.00 + IVA | https://www.dicomse.com.ar/ | Variante DIP8 util para protoboard. |
| ICL7660SCBA | Dicomse | USD 4.00 + IVA | https://www.dicomse.com.ar/ | Variante SOIC8. Precio x5: USD 3.50. |
| ICL7660SCPA | Dicomse | USD 4.00 + IVA | https://www.dicomse.com.ar/ | Variante DIP8 recomendada si se quiere experimentar con fuente negativa simple. |
| Regulador LM2596 modulo step-down | Todomicro | ARS 3381.50 | https://www.todomicro.com.ar/ | Referencia de modulo positivo. No reemplaza una fuente dual. |
| Regulador MT3608 step-up | Todomicro | ARS 1912.13 | https://www.todomicro.com.ar/ | Referencia de modulo elevador positivo. No genera por si solo -V. |

### Proteccion y diodos

| Componente | Fuente | Precio | Link | Nota |
|---|---:|---:|---|---|
| TVS P6SMB12CAT3G | Electrocomponentes | ARS 497 | https://www.electrocomponentes.com/tienda/componentes-electronicos/diodos/diodo-tvs-p6smb12cat3g | Proteccion TVS. |
| TVS SMAJ14A | Electrocomponentes | ARS 223 | https://www.electrocomponentes.com/tienda/componentes-electronicos/diodos/diodo-tvs-smaj14a | Proteccion TVS. |
| TVS P6KE18CA 15 V 600 W bidireccional | Electrocomponentes | ARS 356 | https://www.electrocomponentes.com/tienda/componentes-electronicos/semiconductores-discretos/diodos/tvs/diodo-tvs-15v-600w-bidireccional | Util como proteccion de linea de banco. |
| TVS SMAJ5.0A | Electrocomponentes | ARS 437 | https://www.electrocomponentes.com/tienda/componentes-electronicos/diodos/diodo-tvs-smaj50a | Proteccion a 5 V. Revisar tension de trabajo segun nodo. |
| TVS P6KE30A 25.6 V 600 W | Electrocomponentes | ARS 269 | https://www.electrocomponentes.com/tienda/componentes-electronicos/semiconductores-discretos/diodos/tvs/diodo-tvs-256v-600w-unidireccional | Proteccion para tensiones mayores. |
| TVS 1.5KE30A 25.6 V 1500 W | Electrocomponentes | ARS 602 | https://www.electrocomponentes.com/tienda/componentes-electronicos/semiconductores-discretos/diodos/tvs/diodo-tvs-256v-1500w-unidireccional | Proteccion mas robusta. |
| Schottky 1N5819 | Electrocomponentes | ARS 68 | https://www.electrocomponentes.com/tienda/componentes-electronicos/semiconductores-discretos/diodos/rectificadores/diodo-shottky-1a-40v | Clamp/proteccion simple de laboratorio. |
| Schottky MBR0530T1G | Electrocomponentes | ARS 334 | https://www.electrocomponentes.com/tienda/componentes-electronicos/diodos/diodo-schottky-mbr0530t1g | Schottky SMD. |
| BAV99-7-F | Electrocomponentes | ARS 80 | https://www.electrocomponentes.com/tienda/componentes-electronicos/diodos/array-de-diodos-rectificadores-bav99-7-f | Array de diodos rapido. |
| 1N4148 | Electrocomponentes | ARS 26 | https://www.electrocomponentes.com/tienda/componentes-electronicos/semiconductores-discretos/diodos/rectificadores/diodo-rectificador-simple-300ma-100v-rapido | Diodo rapido discreto. |
| BAT54 | Dicomse | USD 0.50 + IVA | https://www.dicomse.com.ar/ | Schottky SOT23. Hay variantes BAT54A/C/S entre USD 0.50 y USD 0.70. |
| BAV99/T1 | Dicomse | USD 0.40 + IVA | https://www.dicomse.com.ar/ | Array rapido SOT23. Precio x5: USD 0.20. |
| 1N4148 DO35 | Dicomse | USD 0.05 + IVA | https://www.dicomse.com.ar/ | Precio x5: USD 0.04. |

### Pasivos y prototipado

| Componente | Fuente | Precio | Link | Nota |
|---|---:|---:|---|---|
| Capacitor ceramico 100 nF 0805 | Electrocomponentes | ARS 20 | https://www.electrocomponentes.com/tienda/componentes-electronicos/capacitor/capacitor-ceramico-0805-100nf | Desacople comun. |
| Capacitor ceramico 100 nF 0603 | Electrocomponentes | ARS 10 | https://www.electrocomponentes.com/tienda/componentes-electronicos/capacitor/capacitor-ceramico-0603-100nf | Desacople SMD. |
| Capacitor poliester 100 nF 250 V | Electrocomponentes | ARS 190 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/capacitores/film/capacitor-poliester-100nf-250v | Opcion TH. |
| Capacitor electrolitico 10 uF 25 V | Electrocomponentes | ARS 56 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/capacitores/electroliticos-de-aluminio/insercion/capacitor-electrolitico-10uf-25v-mini | Filtrado. |
| Capacitor electrolitico 10 uF 50 V | Electrocomponentes | ARS 61 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/capacitores/electroliticos-de-aluminio/insercion/capacitor-electrolitico-10uf-50v-ele10x50-105 | Filtrado. |
| Capacitor 1 uF 50 V 1206 | Dicomse | USD 0.50 + IVA | https://www.dicomse.com.ar/ | Precio local en Electrocomponentes no verificado para 1 uF. |
| Capacitor electrolitico 1 uF 250 V | Dicomse | USD 0.12 + IVA | https://www.dicomse.com.ar/ | Alternativa TH. |
| Resistencia carbon mini 10 k 1/4 W 5% x50 | Electrocomponentes | sin precio publicado | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/resistencias/fijas/insercion/resistencia-carbon-mini-10k-14w-5-x50u-r250m-010k50 | Aparecio pero sin precio. |
| Resistencia carbon mini valores varios 1/4 W 5% x50 | Electrocomponentes | ARS 1308 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/resistencias/fijas/insercion/resistencia-carbon-mini-100r-14w-5-x50u | No es 1%. Sirve para pruebas no criticas. |
| Resistencia metal film 12 k 1/2 W 5% | Electrocomponentes | ARS 41 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/resistencias/fijas/insercion/resistencia-metal-film-12k-12w-5-sfr16-12k | No es 1%. |
| Resistencia metal film potencia 10 k 1 W 5% | Electrocomponentes | ARS 65 | https://www.electrocomponentes.com/tienda/componentes-electronicos/pasivos/resistencias/fijas/insercion/resistencia-metal-film-de-potencia-10k-1w-5 | No es 1%. |
| Protoboard 830 puntos | Electrocomponentes | ARS 7640 | https://www.electrocomponentes.com/tienda/componentes-electronicos/protoboardsplacas/protoboard-breadboard-simple-830-puntos-experim | Banco de prueba. |
| Protoboard 830 puntos | Todomicro | ARS 2752.72 | https://www.todomicro.com.ar/280-protoboards | Referencia economica encontrada. |
| Bornera hembra 2 vias | Electrocomponentes | ARS 323 | https://www.electrocomponentes.com/tienda/componentes-electronicos/conectores-c01s39s07/borneras/bornera-hembra-2-vias-ajuste-vertical | Conexion de linea. |
| Bornera hembra 3 vias | Electrocomponentes | ARS 511 | https://www.electrocomponentes.com/tienda/componentes-electronicos/conectores-c01s39s07/borneras/bornera-hembra-3-vias-ajuste-vertical-dt128vp-3p | Conexion de linea y GND. |
| Tira de pines 1x40 macho recto | Electrocomponentes | ARS 391 | https://www.electrocomponentes.com/tienda/componentes-electronicos/conectores-c01s39s07/tira-de-pines/tira-de-pines-1x40-macho-recto-th | Conexiones de prototipo. |
| Tira de pines 1x40 macho 90 grados | Electrocomponentes | ARS 434 | https://www.electrocomponentes.com/tienda/componentes-electronicos/conectores-c01s39s07/tira-de-pines/tira-de-pines-1x40-macho-90-th | Conexiones de prototipo. |
| Cables Dupont pack x40 | Todomicro | ARS 3112.20 | https://www.todomicro.com.ar/270-dupont | Electrocomponentes no devolvio resultados para Dupont. |

## Componentes no encontrados con precio local verificable

| Componente | Resultado | Accion sugerida |
|---|---|---|
| MCP6562 | No aparecio con precio en Electrocomponentes, Dicomse ni Todomicro. Microchip/Mouser/Digikey no dieron precio accesible desde este entorno. MercadoLibre queda pendiente por links concretos. | Si aparece en MercadoLibre con precio razonable, conviene priorizarlo para RX frente a LM393. |
| LMV393 | No encontrado con precio local verificable. | Usar LM393 para banco o buscar publicacion concreta. |
| LMV331 | No encontrado con precio local verificable. | Buscar alternativa dual disponible o usar LM393 para banco. |
| TLV3702 | No encontrado localmente. | TI Store: TLV3702IDR con precio oficial USD 0.822, envio/importacion no incluidos. |
| TLV9352 | No encontrado localmente. | TI Store: TLV9352IDR con precio oficial USD 0.340, figuraba sin stock. |
| OPA2197 | No encontrado localmente. | TI Store: OPA2197ID con precio oficial USD 1.440; OPA2197IDR USD 1.200 pero figuraba sin stock. |
| OPA197 | No se tomo precio local ni USD verificable en este relevamiento. | Mantener como alternativa tecnica, no como compra inmediata. |
| MCP6002 | No encontrado en Electrocomponentes. | Relevar MercadoLibre con links concretos o buscar en CABA. |
| Modulo DC/DC bipolar +/-5 V o +/-6 V | No encontrado en Electrocomponentes. | Para banco inicial usar fuente de laboratorio dual o ICL7660 para baja corriente. |
| Resistencias TH 1% en valores 10 k, 12 k, 20 k, 30 k | No aparecieron con precio practico. Electrocomponentes mostro 5% y rollos SMD 1% de 5000 unidades. | Comprar kit 1% por MercadoLibre si se verifica publicacion o consultar mostrador. |
| Capacitor 1 uF 50 V en Electrocomponentes | Aparecio sin precio publicado. | Dicomse tiene 1 uF 50 V SMD a USD 0.50 + IVA. |
| PESD2CAN,215 | Aparecio en Electrocomponentes sin precio. | Usar TVS con precio publicado para banco o consultar stock/precio. |

## Compra minima recomendada para banco

- LM393N: 2 unidades.
- TL082CP o TL072CP: 1 o 2 unidades para ensayos analogicos con fuente dual.
- ICL7660SCPA o ICL7660CPA: 1 o 2 unidades si se quiere generar tension negativa local.
- TVS: al menos 2 unidades para la linea.
- 1N5819 o BAT54/BAV99: varias unidades para clamps de prueba.
- Capacitores 100 nF: 10 unidades.
- Capacitores 10 uF: 4 a 6 unidades.
- Resistencias: comprar valores 10 k, 12 k, 20 k, 30 k preferentemente 1%; si no, usar 5% solo para primera prueba.
- Borneras, pines, Dupont y protoboard.

## Observacion tecnica

Para RX, el LM393 permite empezar rapido porque esta disponible y es barato, pero exige pull-up y no es la opcion mas prolija para el producto final. Si se consigue MCP6562 o LMV393 a precio razonable, conviene evaluar esos comparadores porque son mas adecuados para senales logicas modernas. Para TX bipolar, TL072/TL082 sirven para experimentar con fuente dual, pero para un front-end mas serio conviene elegir un operacional rail-to-rail o un driver dedicado y verificar swing, slew rate, corriente de salida y estabilidad con carga.
