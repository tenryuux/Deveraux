// Projeto Deveraux - kernel de contato do Deformer Graph (corpo)
//
// Mantem a carne macia fora do que ela encosta: um seio nao atravessa o outro, nem um gluteo o outro, nem uma coxa a
// outra; nada entra nas capsulas (maos, bracos e pernas dela, o corpo do parceiro, objetos), nas caixas (assento) e no
// chao. Apertada, a carne achata contra o que encosta e a carne em volta estufa um pouco. Tudo chega a cada quadro no
// array Contatos, do componente UDeverauxContactComponent, no espaco da malha; o lugar de cada coisa no array esta em
// DeverauxBodyMath.h (DeverauxBody::ContactLayout). Este texto vai inteiro no Shader Text do kernel.
//
// Kernel: Custom Compute Kernel no Update Graph, com Execution Domain = Vertex.
//
// Pinos de entrada:
//   NumThreads     Int Vector 3     Parameter   ligado ao Execute Skinned Mesh (dominio Vertex)
//   Position       Vector 3         Vertex x1   a posicao que sai do skin, ja com os morphs
//   RestPosition   Vector 3         Vertex x1   o Position do Skinned Mesh (a pose de referencia, antes do skin)
//   Color          Linear Color     Vertex x1   o Color do Skinned Mesh (a mascara pintada no Blender)
//   Contatos       Vector 4 Array   Parameter   Get Variable Contatos (variavel do grafo, array de Vector 4)
//
// Pino de saida:
//   OutPosition    Vector 3         Vertex x1   vai para o recalculo das normais e para o Write Skinned Mesh
//
// A mascara (vertex color): o vermelho e quanto a carne cede (1 na carne macia, 0 no resto, borda macia). O verde e o
// azul, comparados com o vermelho, dizem a regiao: amarelo (1, 1, 0) seios, magenta (1, 0, 1) gluteos, branco (1, 1, 1)
// coxas e vermelho (1, 0, 0) o resto da carne macia, como a barriga. O lado (esquerdo ou direito) de cada vertice vem da
// pose de referencia; perto do meio do corpo, no fundo do decote e do sulco dos gluteos, o par nao empurra.

// Se a declaracao do pino Contatos, no topo do Shader Text, tiver outro nome ou outro jeito de ler, troque so esta linha
#define CONTATO(I) ReadContatos(I)

if (Index >= ReadNumThreads().x)
{
	return;
}

const float3 Antes = ReadPosition(Index);
const float4 Cor = ReadColor(Index);
const float Mascara = saturate(Cor.r);
float3 P = Antes;

if (Mascara > 0.0f)
{
	// Lugares no array (DeverauxBody::ContactLayout)
	const uint Pares = 2u;
	const uint Apertos = 5u;
	const uint Massas = 7u;
	const uint Chaos = 19u;
	const uint Capsulas = 26u;
	const uint Caixas = 58u;

	const float4 Cabecalho = CONTATO(0u);
	const float4 Ajustes = CONTATO(1u);
	const uint NumCapsulas = min((uint)(Cabecalho.x + 0.5f), 16u);
	const uint NumCaixas = min((uint)(Cabecalho.y + 0.5f), 2u);

	// Regiao pela cor: verde e azul divididos pelo vermelho, porque a borda macia escurece os tres juntos
	const float Verde = saturate(Cor.g / max(Cor.r, 0.0001f));
	const float Azul = saturate(Cor.b / max(Cor.r, 0.0001f));
	const float VerdeCodigo = Verde >= 0.5f ? 1.0f : 0.0f;
	const float AzulCodigo = Azul >= 0.5f ? 1.0f : 0.0f;
	const uint Regiao = VerdeCodigo > 0.5f ? (AzulCodigo > 0.5f ? 2u : 0u) : (AzulCodigo > 0.5f ? 1u : 3u);

	// Lado pela pose de referencia. O plano do par perde forca no meio do corpo e onde duas cores se misturam
	const float3 Repouso = ReadRestPosition(Index);
	const uint Lado = Repouso.x * Ajustes.x >= 0.0f ? 0u : 1u;
	const float Pureza = 1.0f - smoothstep(0.1f, 0.4f, max(abs(Verde - VerdeCodigo), abs(Azul - AzulCodigo)));
	const float PesoPar = Pureza * smoothstep(0.0f, max(Ajustes.y, 0.01f), abs(Repouso.x));
	const uint Massa = Regiao < 3u ? Regiao * 2u + Lado : 6u;
	// Bit da massa (o resto da carne e o 6): cada capsula diz quais massas empurra
	const uint BitMassa = 1u << Massa;

	// Bojo: com a massa apertada, a carne perto do contato, mas fora dele, estufa para longe do eixo da massa
	if (Regiao < 3u && Cabecalho.z > 0.0f && Cabecalho.w > 0.0f)
	{
		const float4 ApertosDoGrupo = CONTATO(Apertos + Massa / 4u);
		const float Aperto = ApertosDoGrupo[Massa % 4u];
		if (Aperto > 0.0f)
		{
			// Distancia ate o contato mais perto que empurra esta carne; negativa se ela ja esta dentro de um
			float Perto = 1.0e9f;
			[loop]
			for (uint C = 0u; C < NumCapsulas; ++C)
			{
				const float4 A = CONTATO(Capsulas + C * 2u);
				const float4 B = CONTATO(Capsulas + C * 2u + 1u);
				if (A.w > 0.0f && ((uint)(B.w + 0.5f) & BitMassa) != 0u)
				{
					const float3 Eixo = B.xyz - A.xyz;
					const float T = saturate(dot(P - A.xyz, Eixo) / max(dot(Eixo, Eixo), 0.000001f));
					Perto = min(Perto, length(P - (A.xyz + Eixo * T)) - A.w);
				}
			}
			[loop]
			for (uint K = 0u; K < NumCaixas; ++K)
			{
				const float4 Centro = CONTATO(Caixas + K * 4u);
				if (Centro.w > 0.0f)
				{
					const float4 EixoX = CONTATO(Caixas + K * 4u + 1u);
					const float4 EixoY = CONTATO(Caixas + K * 4u + 2u);
					const float4 EixoZ = CONTATO(Caixas + K * 4u + 3u);
					const float3 Relativo = P - Centro.xyz;
					const float3 Folga = abs(float3(dot(Relativo, EixoX.xyz), dot(Relativo, EixoY.xyz), dot(Relativo, EixoZ.xyz)))
						- float3(EixoX.w, EixoY.w, EixoZ.w);
					Perto = min(Perto, length(max(Folga, 0.0f)) + min(max(Folga.x, max(Folga.y, Folga.z)), 0.0f));
				}
			}
			const float4 ChaoDaMassa = CONTATO(Chaos + Massa);
			if (dot(ChaoDaMassa.xyz, ChaoDaMassa.xyz) > 0.25f)
			{
				Perto = min(Perto, dot(ChaoDaMassa.xyz, P) - ChaoDaMassa.w);
			}
			const float4 PlanoDoPar = CONTATO(Pares + Regiao);
			if (dot(PlanoDoPar.xyz, PlanoDoPar.xyz) > 0.25f && PesoPar > 0.0f)
			{
				const float Distancia = dot(PlanoDoPar.xyz, P) - PlanoDoPar.w;
				Perto = min(Perto, Lado == 0u ? -Distancia : Distancia);
			}

			const float4 EixoA = CONTATO(Massas + Massa * 2u);
			const float4 EixoB = CONTATO(Massas + Massa * 2u + 1u);
			if (EixoA.w > 0.0f && Perto >= 0.0f && Perto < Cabecalho.w)
			{
				const float3 Eixo = EixoB.xyz - EixoA.xyz;
				const float T = saturate(dot(P - EixoA.xyz, Eixo) / max(dot(Eixo, Eixo), 0.000001f));
				const float3 Fora = P - (EixoA.xyz + Eixo * T);
				const float Tamanho = length(Fora);
				if (Tamanho > 0.0001f)
				{
					const float Queda = 1.0f - Perto / Cabecalho.w;
					P += Fora * (Cabecalho.z * Aperto * Queda * Queda / Tamanho);
				}
			}
		}
	}

	// Duas voltas: sair de uma forma pode jogar a carne em outra (a capsula entre os seios e o plano do par)
	[unroll]
	for (uint Volta = 0u; Volta < 2u; ++Volta)
	{
		// Capsulas: a carne que entrou volta para a superficie, na direcao do eixo para ela
		[loop]
		for (uint C = 0u; C < NumCapsulas; ++C)
		{
			const float4 A = CONTATO(Capsulas + C * 2u);
			const float4 B = CONTATO(Capsulas + C * 2u + 1u);
			if (A.w > 0.0f && ((uint)(B.w + 0.5f) & BitMassa) != 0u)
			{
				const float3 Eixo = B.xyz - A.xyz;
				const float T = saturate(dot(P - A.xyz, Eixo) / max(dot(Eixo, Eixo), 0.000001f));
				const float3 NoEixo = A.xyz + Eixo * T;
				const float3 Fora = P - NoEixo;
				const float Distancia = length(Fora);
				if (Distancia < A.w && Distancia > 0.0001f)
				{
					P = NoEixo + Fora * (A.w / Distancia);
				}
			}
		}

		// Caixas (assento, cama): a carne sai pela face virada para o corpo (+Z da caixa) ou por um lado, o que for mais
		// perto, e achata contra ela. Nunca pela face de tras: numa caixa fina, a carne que afundou mais que meia
		// espessura volta para cima, sem atravessar
		[loop]
		for (uint K = 0u; K < NumCaixas; ++K)
		{
			const float4 Centro = CONTATO(Caixas + K * 4u);
			if (Centro.w > 0.0f)
			{
				const float4 EixoX = CONTATO(Caixas + K * 4u + 1u);
				const float4 EixoY = CONTATO(Caixas + K * 4u + 2u);
				const float4 EixoZ = CONTATO(Caixas + K * 4u + 3u);
				const float3 Relativo = P - Centro.xyz;
				const float3 Local = float3(dot(Relativo, EixoX.xyz), dot(Relativo, EixoY.xyz), dot(Relativo, EixoZ.xyz));
				const float3 Meia = float3(EixoX.w, EixoY.w, EixoZ.w);
				if (all(abs(Local) < Meia))
				{
					const float FundoX = Meia.x - abs(Local.x);
					const float FundoY = Meia.y - abs(Local.y);
					const float FundoZ = Meia.z - Local.z;
					if (FundoZ <= FundoX && FundoZ <= FundoY)
					{
						P += EixoZ.xyz * FundoZ;
					}
					else if (FundoX <= FundoY)
					{
						P += EixoX.xyz * (Local.x >= 0.0f ? FundoX : -FundoX);
					}
					else
					{
						P += EixoY.xyz * (Local.y >= 0.0f ? FundoY : -FundoY);
					}
				}
			}
		}

		// Chao: o de baixo desta massa (ou do resto da carne)
		const float4 Chao = CONTATO(Chaos + Massa);
		if (dot(Chao.xyz, Chao.xyz) > 0.25f)
		{
			const float Altura = dot(Chao.xyz, P) - Chao.w;
			if (Altura < 0.0f)
			{
				P -= Chao.xyz * Altura;
			}
		}

		// Par: a massa esquerda fica atras do plano (contra a normal), a direita na frente; apertadas, achatam nele
		if (Regiao < 3u)
		{
			const float4 Plano = CONTATO(Pares + Regiao);
			if (dot(Plano.xyz, Plano.xyz) > 0.25f)
			{
				const float Distancia = dot(Plano.xyz, P) - Plano.w;
				const float Passou = Lado == 0u ? Distancia : -Distancia;
				if (Passou > 0.0f)
				{
					P -= Plano.xyz * (Distancia * PesoPar);
				}
			}
		}
	}

	// A mascara entre 0 e 1 deixa a borda da area macia ceder so um pouco
	P = lerp(Antes, P, Mascara);
}

WriteOutPosition(Index, P);
