// Projeto Deveraux - kernel de contato do Deformer Graph (corpo)
//
// Empurra para fora das maos (esferas) e do chao e do assento (caixas) a carne que entrou neles, so onde a mascara
// deixa: o canal vermelho do vertex color, 1 na carne macia e 0 no resto. Os contatos chegam a cada quadro do
// componente UDeverauxContactComponent, no espaco da malha. Este texto vai inteiro no Shader Text do kernel.
//
// Kernel: Custom Compute Kernel no Update Graph, com Execution Domain = Vertex.
//
// Pinos de entrada:
//   NumThreads        Int Vector 3   Parameter    ligado ao Execute Skinned Mesh (dominio Vertex)
//   Position          Vector 3       Vertex x1    a posicao que sai do skin, ja com os morphs
//   Color             Linear Color   Vertex x1    o Color do Skinned Mesh (vertex color)
//   Esfera0           Vector 4       Parameter    Get Variable Esfera0 (o mesmo para todos abaixo)
//   Esfera1           Vector 4       Parameter
//   Caixa0Centro      Vector 4       Parameter
//   Caixa0EixoX       Vector 4       Parameter
//   Caixa0EixoY       Vector 4       Parameter
//   Caixa0EixoZ       Vector 4       Parameter
//   Caixa1Centro      Vector 4       Parameter
//   Caixa1EixoX       Vector 4       Parameter
//   Caixa1EixoY       Vector 4       Parameter
//   Caixa1EixoZ       Vector 4       Parameter
//
// Pino de saida:
//   OutPosition       Vector 3       Vertex x1    vai para o recalculo das normais e para o Write Skinned Mesh
//
// As variaveis do grafo (Explorer > Variables) sao todas Vector 4, com os nomes dos pinos acima. Com o jogo
// rodando, o comando Deveraux.Corpo.Contato diz quantas delas o deformer aceitou (10 de 10 = tudo certo).
//
// Formato: Esfera = centro em XYZ e raio em W (raio 0 = desligada). Caixa = centro em XYZ com W 1 (W 0 = desligada)
// e tres eixos unitarios em XYZ, com a meia largura daquele eixo em W.

if (Index >= ReadNumThreads().x)
{
	return;
}

const float3 Antes = ReadPosition(Index);
const float Mascara = saturate(ReadColor(Index).r);
float3 P = Antes;

if (Mascara > 0.0f)
{
	// Maos: o ponto que entrou na esfera volta para a superficie, na direcao do centro para ele
	const float4 Esferas[2] = { ReadEsfera0(), ReadEsfera1() };
	[unroll]
	for (uint E = 0; E < 2; ++E)
	{
		const float3 Fora = P - Esferas[E].xyz;
		const float Distancia = length(Fora);
		if (Esferas[E].w > 0.0f && Distancia < Esferas[E].w && Distancia > 0.0001f)
		{
			P = Esferas[E].xyz + Fora * (Esferas[E].w / Distancia);
		}
	}

	// Chao e assento: o ponto que entrou na caixa sai pela face mais perto, o que achata a carne contra ela
	const float4 Centros[2] = { ReadCaixa0Centro(), ReadCaixa1Centro() };
	const float4 EixosX[2] = { ReadCaixa0EixoX(), ReadCaixa1EixoX() };
	const float4 EixosY[2] = { ReadCaixa0EixoY(), ReadCaixa1EixoY() };
	const float4 EixosZ[2] = { ReadCaixa0EixoZ(), ReadCaixa1EixoZ() };
	[unroll]
	for (uint C = 0; C < 2; ++C)
	{
		if (Centros[C].w > 0.0f)
		{
			const float3 Relativo = P - Centros[C].xyz;
			const float3 Local = float3(dot(Relativo, EixosX[C].xyz), dot(Relativo, EixosY[C].xyz), dot(Relativo, EixosZ[C].xyz));
			const float3 Fundo = float3(EixosX[C].w, EixosY[C].w, EixosZ[C].w) - abs(Local);
			if (all(Fundo > 0.0f))
			{
				const float3 Lado = float3(Local.x >= 0.0f ? 1.0f : -1.0f, Local.y >= 0.0f ? 1.0f : -1.0f, Local.z >= 0.0f ? 1.0f : -1.0f);
				if (Fundo.x <= Fundo.y && Fundo.x <= Fundo.z)
				{
					P += EixosX[C].xyz * (Lado.x * Fundo.x);
				}
				else if (Fundo.y <= Fundo.z)
				{
					P += EixosY[C].xyz * (Lado.y * Fundo.y);
				}
				else
				{
					P += EixosZ[C].xyz * (Lado.z * Fundo.z);
				}
			}
		}
	}

	// A mascara entre 0 e 1 deixa a borda da area macia afundar so um pouco
	P = lerp(Antes, P, Mascara);
}

WriteOutPosition(Index, P);
